#include <cstring>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::ui::detail {
namespace {
/** Script base: `class X extends eve.UIComponent { function build() { ... } }`. */
const char *kUIComponentScript = R"SQ(
if (!("_uiComponents" in eve)) eve._uiComponents <- []
if (!("_flushingUIComponents" in eve)) eve._flushingUIComponents <- false
if (!("_nextUIComponentOwner" in eve)) eve._nextUIComponentOwner <- 0

eve._scheduleUIComponent <- function(component) {
    if (component == null || component._scheduled) return
    component._scheduled = true
    eve._uiComponents.append(component)
}

eve._flushUIComponents <- function() {
    if (eve._flushingUIComponents) return
    eve._flushingUIComponents = true
    local pending = eve._uiComponents
    eve._uiComponents = []
    for (local i = 0; i < pending.len(); i += 1) {
        local component = pending[i]
        component._scheduled = false
        try {
            if (component._mounted && component._parent == null && component.dirty)
                component.updateIfDirty()
        } catch (error) {
            for (local j = i + 1; j < pending.len(); j += 1) eve._uiComponents.append(pending[j])
            eve._flushingUIComponents = false
            throw error
        }
    }
    eve._flushingUIComponents = false
}

::eve_ui_flush_components <- function() {
    eve._flushUIComponents()
}

eve.UIComponent <- class {
    hostName = ""
    dirty = true
    forceFull = false
    props = null
    state = null
    _ui = null
    _parent = null
    _mounted = false
    _scheduled = false
    _children = null
    _nextChildren = null
    _keyedChildren = null
    _eventBindings = null
    _eventOwner = 0

    constructor(uiInstance = null, initialProps = null) {
        _ui = uiInstance
        hostName = ""
        dirty = true
        forceFull = false
        props = initialProps != null ? initialProps : {}
        state = {}
        _parent = null
        _mounted = false
        _scheduled = false
        _children = []
        _nextChildren = []
        _keyedChildren = {}
        _eventBindings = []
        eve._nextUIComponentOwner += 1
        _eventOwner = eve._nextUIComponentOwner
    }

    function setUI(uiInstance) { _ui = uiInstance; return this }

    function _applyProps(nextProps, replace, notify) {
        if (nextProps == null) return this
        if (replace) props = {}
        foreach (key, value in nextProps) props.rawset(key, value)
        if (notify) markDirty()
        return this
    }

    function setProps(nextProps, replace = false) {
        return _applyProps(nextProps, replace, true)
    }

    function mountAs(name) {
        hostName = name
        dirty = true
        forceFull = true
        updateIfDirty()
        return this
    }

    function setState(patch = null) {
        if (patch != null) {
            foreach (key, value in patch) state.rawset(key, value)
        }
        markDirty()
        return this
    }
    function markDirty() {
        dirty = true
        if (_parent != null && _parent != this) _parent.markDirty()
        else if (_mounted) eve._scheduleUIComponent(this)
        return this
    }

    // Compose a persistent child instance inside this component's active build pass.
    function renderChild(component, nextProps = null, replaceProps = false) {
        if (component == null) return null
        component._parent = this
        component.setUI(this.ui())
        component._applyProps(nextProps, replaceProps, false)
        _nextChildren.append(component)
        local firstMount = !component._mounted
        component._nextChildren = []
        component.build()
        component._finishChildPass()
        component.dirty = false
        component._mounted = true
        if (firstMount) component.onMount()
        else component.onUpdated()
        return component
    }

    // Reuse a child component by a stable key. factory is called only for a new key.
    function renderKeyed(key, factory, nextProps = null, replaceProps = false) {
        if (key == null) throw "UIComponent renderKeyed requires a key"
        local component = null
        if (key in _keyedChildren) component = _keyedChildren[key]
        else {
            component = factory()
            if (component == null) throw "UIComponent renderKeyed factory returned null"
            _keyedChildren.rawset(key, component)
        }
        return renderChild(component, nextProps, replaceProps)
    }

    function _bindEvent(kind, id, fn) {
        if (id == null || id == "" || fn == null) return this
        foreach (binding in _eventBindings) {
            if (binding.kind == kind && binding.id == id) {
                binding.fn = fn
                if (_mounted) _syncEventHandlers()
                return this
            }
        }
        _eventBindings.append({ kind = kind, id = id, fn = fn })
        if (_mounted) _syncEventHandlers()
        return this
    }

    function bindClick(id, fn) { return _bindEvent("click", id, fn) }
    function bindChange(id, fn) { return _bindEvent("change", id, fn) }

    function clearEventBindings() {
        _eventBindings = []
        this.ui().componentClearHandlers(_eventOwner)
        return this
    }

    function _rootComponent() {
        local root = this
        while (root._parent != null && root._parent != root) root = root._parent
        return root
    }

    function _syncEventHandlers() {
        local u = this.ui()
        u.componentClearHandlers(_eventOwner)
        local root = _rootComponent()
        if (root.hostName == "" || !u.select(root.hostName)) return
        foreach (binding in _eventBindings) {
            if (binding.kind == "click") u.componentOnClick(_eventOwner, binding.id, binding.fn)
            else u.componentOnChange(_eventOwner, binding.id, binding.fn)
        }
    }

    function _syncEventTree() {
        _syncEventHandlers()
        foreach (child in _children) child._syncEventTree()
    }

    function _containsChild(items, child) {
        foreach (candidate in items) {
            if (candidate == child) return true
        }
        return false
    }

    function _finishChildPass() {
        foreach (child in _children) {
            if (!_containsChild(_nextChildren, child)) child._unmountTree()
        }
        local staleKeys = []
        foreach (key, child in _keyedChildren) {
            if (!_containsChild(_nextChildren, child)) staleKeys.append(key)
        }
        foreach (key in staleKeys) delete _keyedChildren[key]
        _children = _nextChildren
        _nextChildren = []
    }

    function _unmountTree() {
        if (!_mounted) return false
        foreach (child in _children) child._unmountTree()
        _children = []
        _nextChildren = []
        _mounted = false
        _scheduled = false
        dirty = false
        this.ui().componentClearHandlers(_eventOwner)
        onUnmount()
        _parent = null
        return true
    }

    function unmount() {
        if (!_unmountTree()) return false
        local u = this.ui()
        if (hostName != "" && u.select(hostName)) u.setHostVisible(false)
        return true
    }

    // Override in subclass: call this.ui().beginWindow / text / button / end ...
    function build() {}
    function onMount() {}
    function onUpdated() {}
    function onUnmount() {}

    function ui() {
        if (_ui != null) return _ui
        try {
            if (::ui != null) return ::ui
        } catch (e) {}
        _ui = ::eve.UI()
        return _ui
    }

    function updateIfDirty() {
        if (!dirty) return false
        local firstMount = !_mounted
        local u = this.ui()
        _scheduled = false
        dirty = false
        _nextChildren = []
        u.beginBuild()
        try {
        this.build()
        _finishChildPass()
        local name = hostName
        if (name == null || name == "") name = "default"
        hostName = name
        if (forceFull) {
            if (!u.mountBuildAs(name)) throw "UIComponent mount failed: " + name
            forceFull = false
        } else {
            if (!u.remountBuildAs(name)) throw "UIComponent remount failed: " + name
        }
        _mounted = true
        u.setHostVisible(true)
        if (firstMount) onMount()
        else onUpdated()
        _syncEventTree()
        } catch (error) { markDirty(); throw error }
        return true
    }

    function rebuild(force = false) {
        dirty = true
        forceFull = force
        return updateIfDirty()
    }
}
// Note: eve.Component is reserved for script ECS (see exposeECS). Use eve.UIComponent.
)SQ";

}  // namespace

void injectUIComponentClass(ssq::Table &eveTable) {
    HSQUIRRELVM     vm  = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kUIComponentScript, static_cast<SQInteger>(std::strlen(kUIComponentScript)),
                                   "UIComponent.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

}  // namespace eve::ui::detail
