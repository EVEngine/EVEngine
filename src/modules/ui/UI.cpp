#include "ui/UI.h"
#include "ui/DatabasePanel.h"
#include "ui/EditorShell.h"
#include "ui/PcgColorPreviewSync.h"
#include "ui/PcgControllerSelection.h"
#include "ui/PcgDraggableWindow.h"
#include "ui/PcgPhotoModeApplyPlan.h"
#include "ui/PcgPhotoModePanels.h"
#include "ui/PcgPhotoModeRuntimeUI.h"
#include "ui/PcgPhotoModeValues.h"
#include "ui/PcgPhotoModeSession.h"
#include "ui/PcgPhotoModeRanges.h"
#include "ui/PcgPhotoModeColorPicker.h"
#include "ui/PcgScreenshotSavedNotice.h"
#include "ui/PcgLoadingScreen.h"
#include "ui/PcgTooltip.h"
#include "ui/EditorHostCapabilities.h"
#include "ui/UIAutomationCapabilities.h"

#include "ui/Inspector.h"
#include "ui/ScenePanel.h"
#include "ui/Theme.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "ui/ParentScaler.h"

#include "common/Module.h"
#include "common/Json.h"
#include "common/Value.h"
#include "common/SquirrelBinding.h"
#include "common/config.h"
#include "graphics/Graphics.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "window/sdl/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <SDL2/SDL_events.h>

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace eve::ui {
namespace {

std::string jsonQuoted(const std::string &value) {
    std::string out = "\"";
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch >= 0x20) out += static_cast<char>(ch);
            break;
        }
    }
    out += '\"';
    return out;
}

/** Call a registered script handler with the event's payload. */
void callScriptHandler(ssq::Function &fn, const std::string &kind, const UIEvent &ev) {
    HSQUIRRELVM vm = fn.getHandle();
    if (!vm) return;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, fn.getRaw());
    sq_pushroottable(vm);  // env
    if (kind == "click") {
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return;
        }
    } else {
        sq_pushstring(vm, kind.c_str(), -1);
        if (kind == "toggle") sq_pushbool(vm, ev.toggleValue ? SQTrue : SQFalse);
        else if (kind == "value") sq_pushfloat(vm, ev.floatValue);
        else sq_pushstring(vm, ev.textValue.c_str(), -1);
        if (SQ_FAILED(sq_call(vm, 3, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return;
        }
    }
    sq_settop(vm, top);
}

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
// JSON UI asset helpers (defined below saveTreeJson/loadTreeJson).
void nodeToJson(const UIHost::Tree &tree, const UINode &n, Poco::JSON::Object &o);
WidgetDesc descFromJson(const Poco::JSON::Object &o);
#endif

/** Script base: `class X extends eve.UIComponent { function build() { ... } }`. */
const char *kUIComponentScript = R"SQ(
if (!("_uiComponents" in eve)) eve._uiComponents <- []
if (!("_nextUIComponentOwner" in eve)) eve._nextUIComponentOwner <- 0

eve._scheduleUIComponent <- function(component) {
    if (component == null || component._scheduled) return
    component._scheduled = true
    eve._uiComponents.append(component)
}

eve._flushUIComponents <- function() {
    local pending = eve._uiComponents
    eve._uiComponents = []
    foreach (component in pending) {
        component._scheduled = false
        if (component._mounted && component._parent == null && component.dirty)
            component.updateIfDirty()
    }
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
        component.setUI(ui())
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
        ui().componentClearHandlers(_eventOwner)
        return this
    }

    function _rootComponent() {
        local root = this
        while (root._parent != null && root._parent != root) root = root._parent
        return root
    }

    function _syncEventHandlers() {
        local u = ui()
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
        ui().componentClearHandlers(_eventOwner)
        onUnmount()
        _parent = null
        return true
    }

    function unmount() {
        if (!_unmountTree()) return false
        local u = ui()
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
        local u = ui()
        _scheduled = false
        dirty = false
        _nextChildren = []
        u.beginBuild()
        build()
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

void injectUIComponentClass(ssq::Table &eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kUIComponentScript,
                                   static_cast<SQInteger>(std::strlen(kUIComponentScript)),
                                   "UIComponent.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

}  // namespace

Module_IMPL(UI, new UI());

UI::UI() : backend_(createImGuiBackend()) {
    registerEditorHostCapabilities();
    registerUIAutomationCapabilities();
}
UI::~UI() { shutdownBackend(); }

bool UI::isBackendReady() const { return backend_ && backend_->isInitialized(); }

bool UI::initBackend() {
    if (!backend_) backend_ = createImGuiBackend();
    if (backend_->isInitialized()) return true;
    auto *win = eve::ModuleManager::getInstance<eve::window::Window>("Window");
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!win || !gfx) return false;
    auto *sdlWin = dynamic_cast<eve::window::sdl::Window *>(win);
    if (!sdlWin) return false;
    auto *native = static_cast<SDL_Window *>(sdlWin->getHandle());
    if (!native) return false;
    const bool ok = backend_->init(native, gfx);
    if (ok) UISystem::setBackend(*backend_);
    return ok;
}

void UI::shutdownBackend() {
    releaseNinePatches();
    UISystem::clearBackend();
    if (backend_) backend_->shutdown();
}

void UI::processEvent(const SDL_Event *event) {
    if (event && UISystem::dragDropSupport() == DragDropSupport::Supported &&
        event->type == SDL_DROPFILE && event->drop.file)
        UISystem::enqueuePlatformFileDrop(event->drop.file);
    if (backend_) backend_->processEvent(event);
}

void UI::beginFrameAndRender() {
    if (!isBackendReady()) {
        if (!initBackend()) return;
    }
    updateHostTweens();
    if (inspector_ && inspector_->isOpen()) inspector_->sync();
    if (databasePanel_ && databasePanel_->isOpen()) databasePanel_->sync();
    backend_->newFrame();
    UISystem::render();
}

void UI::updateHostTweens() {
    if (hostTweens_.empty() && itemTweens_.empty()) return;
    const double now =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    for (auto &t : hostTweens_) {
        auto host = UIHost::resolve(t.host);
        if (!host) continue;
        auto         m       = host->get().meta();
        const double elapsed = now - t.startMs;
        if (t.durationMs <= 0.0 || elapsed >= t.durationMs) {
            m->hasPos = true;
            m->posX = t.toX;
            m->posY = t.toY;
            t.host    = {};  // done; removed below
            continue;
        }
        const float k = float(elapsed / t.durationMs);
        const float ease = k * k * (3.f - 2.f * k);  // smoothstep
        m->hasPos = true;
        m->posX = t.fromX + (t.toX - t.fromX) * ease;
        m->posY = t.fromY + (t.toY - t.fromY) * ease;
    }
    hostTweens_.erase(std::remove_if(hostTweens_.begin(), hostTweens_.end(),
                                     [](const HostTween &t) { return !UIHost::resolve(t.host).has_value(); }),
                      hostTweens_.end());
    for (auto &t : itemTweens_) {
        auto host = UIHost::resolve(t.host);
        if (!host) continue;
        auto node = host->get().findById(t.nodeId);
        if (!node) {
            t.host = {};
            continue;
        }
        const double elapsed = now - t.startMs;
        if (t.durationMs <= 0.0 || elapsed >= t.durationMs) {
            node->get().opacity = t.to;
            t.host = {};
            continue;
        }
        const float k = float(elapsed / t.durationMs);
        const float ease = k * k * (3.f - 2.f * k);
        node->get().opacity = t.from + (t.to - t.from) * ease;
    }
    itemTweens_.erase(
        std::remove_if(itemTweens_.begin(), itemTweens_.end(),
                       [](const ItemTween &t) { return !UIHost::resolve(t.host).has_value(); }),
        itemTweens_.end());
}

void UI::dispatchEvents() {
    // Copy before dispatch: UISystem::dispatchEvents() consumes the pending list.
    const std::vector<UIEvent> events = UISystem::pendingEvents();
    UISystem::dispatchEvents();
    for (const auto &ev : events) {
        if (!UIHost::resolve(ev.host)) continue;
        fireScriptHandlers(ev);
    }
}

void UI::fireScriptHandlers(const UIEvent &ev) {
    for (const auto &h : scriptHandlers_) {
        if (h.hostName != ev.hostName || h.nodeId != ev.nodeId) continue;
        if (h.kind == "click" && ev.kind == "click") {
            ssq::Function fn = h.fn;
            callScriptHandler(fn, "click", ev);
        } else if (h.kind == ev.kind &&
                   (ev.kind == "toggle" || ev.kind == "value" || ev.kind == "text")) {
            ssq::Function fn = h.fn;
            callScriptHandler(fn, ev.kind, ev);
        }
    }
}

void UI::onClick(const std::string &id, ssq::Function fn) {
    auto host = resolveSelected();
    if (!host || id.empty()) return;
    scriptHandlers_.push_back(ScriptHandler(host->get().getName(), id, "click", std::move(fn)));
}

void UI::onChange(const std::string &id, ssq::Function fn) {
    auto host = resolveSelected();
    if (!host || id.empty()) return;
    for (const char *kind : {"toggle", "value", "text"}) {
        scriptHandlers_.push_back(ScriptHandler(host->get().getName(), id, kind, fn));
    }
}

void UI::componentOnClick(uint64_t owner, const std::string &id, ssq::Function fn) {
    auto host = resolveSelected();
    if (!host || owner == 0 || id.empty()) return;
    scriptHandlers_.push_back(
        ScriptHandler(host->get().getName(), id, "click", std::move(fn), owner));
}

void UI::componentOnChange(uint64_t owner, const std::string &id, ssq::Function fn) {
    auto host = resolveSelected();
    if (!host || owner == 0 || id.empty()) return;
    for (const char *kind : {"toggle", "value", "text"}) {
        scriptHandlers_.push_back(ScriptHandler(host->get().getName(), id, kind, fn, owner));
    }
}

void UI::componentClearHandlers(uint64_t owner) {
    if (owner == 0) return;
    std::erase_if(scriptHandlers_, [owner](const ScriptHandler &handler) {
        return handler.owner == owner;
    });
}

bool UI::wantCaptureMouse() const {
    return backend_ ? backend_->wantCaptureMouse() : false;
}

bool UI::wantCaptureKeyboard() const {
    return backend_ ? backend_->wantCaptureKeyboard() : false;
}

UIHostHandle UI::findHost(const std::string &name) const { return UISystem::findHost(name); }

UIHostHandle UI::findHostByOwner(uint32_t ownerId) const { return UISystem::findHostByOwner(ownerId); }

bool UI::select(const std::string &name) {
    const UIHostHandle h = findHost(name);
    if (!UIHost::resolve(h)) return false;
    selected_ = h;
    return true;
}

void UI::bindOwner(uint32_t ownerId) {
    if (auto host = resolveSelected()) host->get().setOwnerId(ownerId);
}

UIHostHandle UI::ensureSelected(const std::string &preferredName) {
    if (UIHost::resolve(selected_)) return selected_;
    selected_ = {};
    if (!preferredName.empty()) {
        const UIHostHandle h = findHost(preferredName);
        if (UIHost::resolve(h)) {
            selected_ = h;
            return selected_;
        }
        selected_ = UIHost::createHost(preferredName);
        return selected_;
    }
    selected_ = UIHost::createHost("default");
    return selected_;
}

UIHostHandle UI::mountAs(const std::string &name, WidgetDesc root) {
    UIHostHandle handle = findHost(name);
    auto         host   = UIHost::resolve(handle);
    if (!host) {
        handle = UIHost::createHost(name);
        host   = UIHost::resolve(handle);
    } else {
        host->get().setName(name);
    }
    if (!host) return {};
    host->get().setTree(std::move(root));
    selected_ = handle;
    return handle;
}

UIHostHandle UI::mount(WidgetDesc root) {
    if (auto host = resolveSelected()) {
        host->get().setTree(std::move(root));
        return selected_;
    }
    return mountAs("default", std::move(root));
}

UIHostHandle UI::remount(WidgetDesc root) {
    const UIHostHandle handle = ensureSelected();
    if (auto host = UIHost::resolve(handle)) host->get().setTree(std::move(root));
    return handle;
}

UIHostHandle UI::remountReconcile(WidgetDesc root) {
    const UIHostHandle handle = ensureSelected();
    if (auto host = UIHost::resolve(handle)) host->get().setTreeReconcile(std::move(root));
    return handle;
}

UIHostHandle UI::remountAs(const std::string &name, WidgetDesc root) { return mountAs(name, std::move(root)); }

eve::OptionalRef<UIHost> UI::resolveSelected() const noexcept { return UIHost::resolve(selected_); }

void UI::beginBuild() {
    openStack_.clear();
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
}

void UI::pushOpen(WidgetDesc d) { openStack_.push_back(std::move(d)); }

WidgetDesc &UI::currentParent() {
    if (openStack_.empty()) throw std::runtime_error("ui: widget outside beginWindow/beginGroup");
    return openStack_.back();
}

void UI::beginWindow(const std::string &title, const std::string &id) {
    if (openStack_.empty() && hasBuiltRoot_) beginBuild();
    pushOpen(window(title, {}, id));
}

void UI::beginGroup(const std::string &id) { pushOpen(group({}, id)); }

void UI::beginList(const std::string &id) { pushOpen(group({}, id)); }

void UI::beginCollapsing(const std::string &label, const std::string &id, bool open) {
    pushOpen(collapsingHeader(label, {}, id, open));
}

void UI::beginChild(const std::string &id, float width, float height) {
    pushOpen(child(id, {}, width, height));
}

void UI::beginCard(const std::string &id) { pushOpen(card({}, id)); }

bool UI::beginNinePatch(const std::string &path, const std::string &id, float width,
                        float height) {
    auto *asset = loadNinePatch(path);
    if (!asset) return false;
    WidgetDesc d = ninePatchPanel({}, id, asset->textureId);
    d.sizeX = width;
    d.sizeY = height;
    d.minSizeX = float(asset->info.width);
    d.minSizeY = float(asset->info.height);
    d.borderL = float(asset->info.borderLeft);
    d.borderT = float(asset->info.borderTop);
    d.borderR = float(asset->info.borderRight);
    d.borderB = float(asset->info.borderBottom);
    d.paddingL = float(asset->info.paddingLeft);
    d.paddingT = float(asset->info.paddingTop);
    d.paddingR = float(asset->info.paddingRight);
    d.paddingB = float(asset->info.paddingBottom);
    pushOpen(std::move(d));
    return true;
}

void UI::beginMenuBar(const std::string &id) { pushOpen(menuBar({}, id)); }

void UI::beginMenu(const std::string &label, const std::string &id) {
    pushOpen(menu(label, {}, id));
}

void UI::beginToolbar(const std::string &id) { pushOpen(toolbar({}, id)); }

void UI::beginToolbox(const std::string &id, float cellSize, int columns) {
    pushOpen(toolbox({}, id, cellSize, columns));
}

void UI::beginSidebar(const std::string &id, float width) {
    pushOpen(sidebar({}, id, width));
}

void UI::beginStatusBar(const std::string &id) { pushOpen(statusBar({}, id)); }

void UI::beginScrollList(const std::string &id, float height, float itemHeight) {
    pushOpen(scrollList(id, {}, height, itemHeight));
}

namespace {

std::string toLowerCopy(std::string s) {
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

FlexDirection parseFlexDirection(const std::string &direction) {
    const std::string d = toLowerCopy(direction);
    if (d == "column" || d == "col" || d == "vertical" || d == "v") return FlexDirection::Column;
    return FlexDirection::Row;
}

FlexAlign parseFlexAlign(const std::string &align) {
    const std::string a = toLowerCopy(align);
    if (a == "center") return FlexAlign::Center;
    if (a == "end" || a == "right" || a == "bottom") return FlexAlign::End;
    if (a == "stretch") return FlexAlign::Stretch;
    return FlexAlign::Start;
}

FlexJustify parseFlexJustify(const std::string &justify) {
    const std::string j = toLowerCopy(justify);
    if (j == "center") return FlexJustify::Center;
    if (j == "end" || j == "right" || j == "bottom") return FlexJustify::End;
    if (j == "spacebetween" || j == "space-between" || j == "between")
        return FlexJustify::SpaceBetween;
    if (j == "spacearound" || j == "space-around" || j == "around") return FlexJustify::SpaceAround;
    return FlexJustify::Start;
}

OverflowMode parseOverflowMode(const std::string &overflow) {
    const std::string value = toLowerCopy(overflow);
    if (value == "clip" || value == "hidden") return OverflowMode::Clip;
    if (value == "scroll" || value == "auto") return OverflowMode::Scroll;
    return OverflowMode::Visible;
}

FocusMode parseFocusMode(const std::string &mode) {
    const std::string value = toLowerCopy(mode);
    if (value == "none") return FocusMode::None;
    if (value == "click") return FocusMode::Click;
    return FocusMode::All;
}

MouseFilter parseMouseFilter(const std::string &filter) {
    const std::string value = toLowerCopy(filter);
    if (value == "pass") return MouseFilter::Pass;
    if (value == "ignore") return MouseFilter::Ignore;
    return MouseFilter::Stop;
}

ThemePreset parseThemePreset(const std::string &theme) {
    const std::string value = toLowerCopy(theme);
    if (value == "dark") return ThemePreset::Dark;
    if (value == "light") return ThemePreset::Light;
    return ThemePreset::Inherit;
}

AccessibilityRole parseAccessibilityRole(const std::string &role) {
    const std::string value = toLowerCopy(role);
    if (value == "button") return AccessibilityRole::Button;
    if (value == "checkbox") return AccessibilityRole::Checkbox;
    if (value == "slider") return AccessibilityRole::Slider;
    if (value == "text") return AccessibilityRole::Text;
    if (value == "textinput" || value == "text-input") return AccessibilityRole::TextInput;
    if (value == "list") return AccessibilityRole::List;
    if (value == "listitem" || value == "list-item") return AccessibilityRole::ListItem;
    if (value == "menu") return AccessibilityRole::Menu;
    if (value == "menuitem" || value == "menu-item") return AccessibilityRole::MenuItem;
    if (value == "progress") return AccessibilityRole::Progress;
    if (value == "region") return AccessibilityRole::Region;
    if (value == "tab") return AccessibilityRole::Tab;
    if (value == "window") return AccessibilityRole::Window;
    return AccessibilityRole::Auto;
}

bool parseFocusDirection(const std::string &direction, FocusDirection *out) {
    if (!out) return false;
    const std::string value = toLowerCopy(direction);
    if (value == "next") *out = FocusDirection::Next;
    else if (value == "previous" || value == "prev") *out = FocusDirection::Previous;
    else if (value == "left") *out = FocusDirection::Left;
    else if (value == "right") *out = FocusDirection::Right;
    else if (value == "up") *out = FocusDirection::Up;
    else if (value == "down") *out = FocusDirection::Down;
    else return false;
    return true;
}

}  // namespace

void UI::beginFlex(const std::string &direction, const std::string &id, float gap) {
    WidgetDesc d = flex(parseFlexDirection(direction), {}, id);
    d.gap = gap;
    pushOpen(std::move(d));
}

void UI::beginRow(const std::string &id, float gap) { beginFlex("row", id, gap); }

void UI::beginColumn(const std::string &id, float gap) { beginFlex("column", id, gap); }

void UI::beginGrid(int columns, const std::string &id, float columnGap, float rowGap) {
    WidgetDesc d = grid(columns, {}, id);
    d.columnGap = columnGap;
    d.rowGap = rowGap;
    pushOpen(std::move(d));
}

void UI::beginSplitPane(const std::string &direction, float ratio, const std::string &id) {
    WidgetDesc d;
    d.type = NodeType::SplitPane;
    d.id = id;
    d.key = id;
    d.flexDirection = parseFlexDirection(direction);
    d.value = std::max(0.1f, std::min(0.9f, ratio));
    d.minValue = 0.1f;
    d.maxValue = 0.9f;
    pushOpen(std::move(d));
}

void UI::end() {
    if (openStack_.empty()) throw std::runtime_error("ui: end() without begin");
    WidgetDesc finished = std::move(openStack_.back());
    openStack_.pop_back();
    if (openStack_.empty()) {
        builtRoot_ = std::move(finished);
        hasBuiltRoot_ = true;
    } else {
        openStack_.back().children.push_back(std::move(finished));
    }
}

void UI::addText(const std::string &content, const std::string &id) {
    currentParent().children.push_back(text(content, id));
}

void UI::addTextWrapped(const std::string &content, float width, const std::string &id) {
    WidgetDesc d = text(content, id);
    d.wrapWidth = width;
    currentParent().children.push_back(std::move(d));
}

void UI::addButton(const std::string &label, const std::string &id) {
    currentParent().children.push_back(button(label, id));
}

void UI::addIcon(const std::string &name, const std::string &id) {
    Icon value = Icon::None;
    iconFromName(name, &value);
    currentParent().children.push_back(icon(value, id));
}

void UI::addIconButton(const std::string &name, const std::string &label, const std::string &id) {
    Icon value = Icon::None;
    iconFromName(name, &value);
    currentParent().children.push_back(iconButton(value, label, id));
}

void UI::addSameLine(const std::string &id) { currentParent().children.push_back(sameLine(id)); }

void UI::addSeparator(const std::string &id) {
    currentParent().children.push_back(separator(id));
}

void UI::addCheckbox(const std::string &label, bool checked, const std::string &id) {
    currentParent().children.push_back(checkbox(label, checked, id));
}

void UI::addSlider(const std::string &label, float value, float minV, float maxV,
                   const std::string &id) {
    currentParent().children.push_back(slider(label, value, minV, maxV, id));
}

void UI::addColorPalette(const std::string &label, float r, float g, float b, float a,
                         const std::string &id) {
    currentParent().children.push_back(colorPalette(label, r, g, b, a, id));
}

void UI::addProgress(float fraction, const std::string &id, const std::string &overlay) {
    currentParent().children.push_back(progress(fraction, id, overlay));
}

void UI::addImage(const std::string &id, float width, float height) {
    currentParent().children.push_back(image(id, width, height));
}

bool UI::addNinePatch(const std::string &path, const std::string &id, float width,
                      float height) {
    auto *asset = loadNinePatch(path);
    if (!asset) return false;
    WidgetDesc d = image(id, width > 0.f ? width : float(asset->info.width),
                         height > 0.f ? height : float(asset->info.height));
    d.textureId = asset->textureId;
    d.borderL = float(asset->info.borderLeft);
    d.borderT = float(asset->info.borderTop);
    d.borderR = float(asset->info.borderRight);
    d.borderB = float(asset->info.borderBottom);
    currentParent().children.push_back(std::move(d));
    return true;
}

void UI::addImageButton(const std::string &id, float width, float height) {
    currentParent().children.push_back(imageButton(id, width, height));
}

void UI::addViewport(const std::string &id, float width, float height) {
    currentParent().children.push_back(viewport(id, width, height));
}

void UI::addCombo(const std::string &label, const std::string &options, int selected,
                  const std::string &id) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= options.size()) {
        const size_t end = options.find('\n', start);
        items.push_back(options.substr(start, end == std::string::npos ? std::string::npos
                                                                       : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    currentParent().children.push_back(combo(label, items, selected, id));
}

void UI::addInputText(const std::string &label, const std::string &value, const std::string &id) {
    currentParent().children.push_back(inputText(label, value, id));
}

void UI::addSearchField(const std::string &hint, const std::string &value,
                        const std::string &id) {
    currentParent().children.push_back(searchField(hint, value, id));
}

void UI::addSwitch(const std::string &label, bool checked, const std::string &id) {
    currentParent().children.push_back(toggleSwitch(label, checked, id));
}

void UI::addBadge(const std::string &label, const std::string &id) {
    currentParent().children.push_back(badge(label, id));
}

void UI::addSectionHeader(const std::string &label, const std::string &id) {
    currentParent().children.push_back(sectionHeader(label, id));
}

void UI::addMenuItem(const std::string &label, const std::string &shortcut,
                     const std::string &id) {
    currentParent().children.push_back(menuItem(label, shortcut, id));
}

void UI::addSpacer(const std::string &id, float grow) {
    currentParent().children.push_back(spacer(id, grow));
}

void UI::setItemFlexGrow(float grow) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().flexGrow = grow;
}

void UI::setItemFlexShrink(float shrink) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().flexShrink = std::max(0.f, shrink);
}

void UI::setItemFlexBasis(float basis) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().flexBasis = basis;
}

void UI::setItemAlignSelf(const std::string &align) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().alignSelf = align == "inherit" ? -1 : int(parseFlexAlign(align));
}

void UI::setItemAspectRatio(float ratio) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().aspectRatio = std::max(0.f, ratio);
}

void UI::setItemGridColumnSpan(int span) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().gridColumnSpan = std::max(1, span);
}

void UI::setItemSize(float width, float height) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().sizeX = width;
    parent.children.back().sizeY = height;
}

void UI::setItemMargin(float l, float t, float r, float b) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().marginL = l;
    parent.children.back().marginT = t;
    parent.children.back().marginR = r;
    parent.children.back().marginB = b;
}

void UI::setItemPadding(float l, float t, float r, float b) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().paddingL = l;
    parent.children.back().paddingT = t;
    parent.children.back().paddingR = r;
    parent.children.back().paddingB = b;
}

void UI::setItemMinSize(float w, float h) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().minSizeX = w;
    parent.children.back().minSizeY = h;
}

void UI::setItemMaxSize(float w, float h) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().maxSizeX = w;
    parent.children.back().maxSizeY = h;
}

void UI::setItemPercent(float w, float h) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().percentW = w;
    parent.children.back().percentH = h;
}

void UI::setItemAbsolute(float anchorX, float anchorY, float x, float y) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().absolute = true;
    parent.children.back().anchorX = anchorX;
    parent.children.back().anchorY = anchorY;
    parent.children.back().posX = x;
    parent.children.back().posY = y;
}

void UI::setItemTooltip(const std::string &text) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().tooltip = text;
}

void UI::setItemDragSource(const std::string &payloadType, const std::string &payloadText) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    auto &item = parent.children.back();
    item.dragSource = !payloadType.empty();
    item.dragPayloadType = payloadType;
    item.dragPayloadText = payloadText;
}

void UI::setItemDropTarget(const std::string &acceptedType) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    auto &item = parent.children.back();
    item.dropTarget = !acceptedType.empty();
    item.acceptedDropType = acceptedType;
}

void UI::setItemEnabled(bool enabled) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().enabled = enabled;
}

void UI::setItemSelected(bool selected) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().checked = selected;
}

void UI::setItemFocusMode(const std::string &mode) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().focusMode = parseFocusMode(mode);
}

void UI::setItemMouseFilter(const std::string &filter) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().mouseFilter = parseMouseFilter(filter);
}

void UI::setItemTheme(const std::string &theme) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().themePreset = parseThemePreset(theme);
}

void UI::setThemeScope(const std::string &theme) {
    currentParent().themePreset = parseThemePreset(theme);
}

void UI::setItemStyleClass(const std::string &name) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().styleClass = name;
}

void UI::setStyleScope(const std::string &name) { currentParent().styleClass = name; }

void UI::setItemTabIndex(int index) {
    WidgetDesc &parent = currentParent();
    if (!parent.children.empty()) parent.children.back().tabIndex = index;
}

void UI::setItemFocusOrder(const std::string &previous, const std::string &next) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().focusPrevious = previous;
    parent.children.back().focusNext = next;
}

void UI::setItemFocusNeighbors(const std::string &left, const std::string &right,
                               const std::string &up, const std::string &down) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().focusLeft = left;
    parent.children.back().focusRight = right;
    parent.children.back().focusUp = up;
    parent.children.back().focusDown = down;
}

void UI::setItemAccessibility(const std::string &role, const std::string &name,
                              const std::string &description) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().accessibilityRole = parseAccessibilityRole(role);
    parent.children.back().accessibilityName = name;
    parent.children.back().accessibilityDescription = description;
}

void UI::setFlexAlign(const std::string &align) {
    WidgetDesc &parent = currentParent();
    if (parent.type != NodeType::Flex) return;
    parent.alignItems = parseFlexAlign(align);
}

void UI::setFlexJustify(const std::string &justify) {
    WidgetDesc &parent = currentParent();
    if (parent.type != NodeType::Flex) return;
    parent.justifyContent = parseFlexJustify(justify);
}

void UI::setLayoutGaps(float columnGap, float rowGap) {
    WidgetDesc &parent = currentParent();
    if (parent.type != NodeType::Flex && parent.type != NodeType::Grid) return;
    parent.columnGap = columnGap;
    parent.rowGap = rowGap;
}

void UI::setFlexWrap(bool wrap) {
    WidgetDesc &parent = currentParent();
    if (parent.type == NodeType::Flex)
        parent.flexWrap = wrap ? FlexWrap::Wrap : FlexWrap::NoWrap;
}

void UI::setLayoutOverflow(const std::string &overflow) {
    WidgetDesc &parent = currentParent();
    if (parent.type == NodeType::Flex || parent.type == NodeType::Grid)
        parent.overflow = parseOverflowMode(overflow);
}

void UI::addListItem(const std::string &label, const std::string &id) {
    WidgetDesc &parent = currentParent();
    std::string itemId = id;
    if (itemId.empty())
        itemId = parent.id + "/" + std::to_string(parent.children.size());
    parent.children.push_back(button(label, itemId).withKey(itemId));
}

bool UI::buildComplete() const { return openStack_.empty() && hasBuiltRoot_; }

bool UI::mountBuild() {
    if (!buildComplete()) return false;
    const UIHostHandle handle = remount(std::move(builtRoot_));
    if (!UIHost::resolve(handle)) return false;
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
    return true;
}

bool UI::mountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    const UIHostHandle handle = mountAs(name, std::move(builtRoot_));
    if (!UIHost::resolve(handle)) return false;
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
    return true;
}

bool UI::remountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    UIHostHandle h = findHost(name);
    if (!UIHost::resolve(h)) h = UIHost::createHost(name);
    auto host = UIHost::resolve(h);
    if (!host) return false;
    host->get().setTreeReconcile(std::move(builtRoot_));
    selected_ = h;
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
    return true;
}

bool UI::setListItems(const std::string &listId, const std::vector<std::string> &items) {
    auto host = resolveSelected();
    if (!host) return false;
    WidgetDesc listNode = listButtons(listId, items);
    auto       existing = host->get().findById(listId);
    if (existing && existing->get().type == NodeType::Group) {
        host->get().setTreeReconcile(
            window(host->get().getName().empty() ? "List" : host->get().getName(), {std::move(listNode)}, "root"));
        return true;
    }
    host->get().setTree(
        window(host->get().getName().empty() ? "List" : host->get().getName(), {std::move(listNode)}, "root"));
    return true;
}

void UI::setText(const std::string &id, const std::string &text) {
    if (auto host = resolveSelected()) host->get().setTextById(id, text);
}

void UI::setTextWrap(const std::string &id, float width) {
    auto host = resolveSelected();
    if (!host) return;
    if (auto n = host->get().findById(id)) n->get().wrapWidth = width;
}

void UI::setVisible(const std::string &id, bool visible) {
    if (auto host = resolveSelected()) host->get().setVisibleById(id, visible);
}

void UI::setEnabled(const std::string &id, bool enabled) {
    if (auto host = resolveSelected()) host->get().setEnabledById(id, enabled);
}

void UI::setChecked(const std::string &id, bool checked) {
    if (auto host = resolveSelected()) host->get().setCheckedById(id, checked);
}

void UI::setValue(const std::string &id, float value) {
    if (auto host = resolveSelected()) host->get().setValueById(id, value);
}

void UI::setValueText(const std::string &id, const std::string &value) {
    if (auto host = resolveSelected()) host->get().setValueTextById(id, value);
}

void UI::setColor(const std::string &id, float r, float g, float b, float a) {
    setImageTint(id, r, g, b, a);
}

void UI::setImageTint(const std::string &id, float r, float g, float b, float a) {
    auto host = resolveSelected();
    if (!host) return;
    if (auto n = host->get().findById(id)) {
        n->get().tintR = r;
        n->get().tintG = g;
        n->get().tintB = b;
        n->get().tintA = a;
    }
}

void UI::setImageUv(const std::string &id, float u0, float v0, float u1, float v1) {
    auto host = resolveSelected();
    if (!host) return;
    if (auto n = host->get().findById(id)) {
        n->get().uv0x = u0;
        n->get().uv0y = v0;
        n->get().uv1x = u1;
        n->get().uv1y = v1;
    }
}

void UI::setImageNinePatch(const std::string &id, float l, float t, float r, float b) {
    auto host = resolveSelected();
    if (!host) return;
    if (auto n = host->get().findById(id)) {
        n->get().borderL = l;
        n->get().borderT = t;
        n->get().borderR = r;
        n->get().borderB = b;
    }
}

void UI::setImageCornerRadius(const std::string &id, float radius) {
    auto host = resolveSelected();
    if (!host) return;
    if (auto n = host->get().findById(id)) n->get().cornerRadius = radius;
}

void UI::setImageTextureId(const std::string &id, uint64_t textureId) {
    auto host = resolveSelected();
    if (!host) return;
    if (auto n = host->get().findById(id)) n->get().textureId = textureId;
}

uint64_t UI::registerTexture(graphics::Texture *tex) {
    if (!isBackendReady()) {
        if (!initBackend()) return 0;
    }
    return backend_ ? backend_->registerTexture(tex) : 0;
}

void UI::unregisterTexture(uint64_t textureId) {
    if (backend_ && textureId != 0) backend_->unregisterTexture(textureId);
}

float UI::getValue(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto n = host->get().findById(id)) return n->get().value;
    return 0.f;
}

std::string UI::getValueText(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return {};
    if (auto n = host->get().findById(id)) return n->get().valueText;
    return {};
}

float UI::getColorR(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto n = host->get().findById(id)) return n->get().tintR;
    return 0.f;
}

float UI::getColorG(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto n = host->get().findById(id)) return n->get().tintG;
    return 0.f;
}

float UI::getColorB(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto n = host->get().findById(id)) return n->get().tintB;
    return 0.f;
}

float UI::getColorA(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto n = host->get().findById(id)) return n->get().tintA;
    return 0.f;
}

bool UI::getChecked(const std::string &id) const {
    auto host = resolveSelected();
    if (!host) return false;
    if (auto n = host->get().findById(id)) return n->get().checked;
    return false;
}

bool UI::setImageNinePatchFile(const std::string &id, const std::string &path) {
    auto host = resolveSelected();
    if (!host) return false;
    auto n = host->get().findById(id);
    if (!n || (n->get().type != NodeType::Image && n->get().type != NodeType::ImageButton &&
               n->get().type != NodeType::NinePatchPanel))
        return false;
    auto *asset = loadNinePatch(path);
    if (!asset) return false;
    n->get().textureId = asset->textureId;
    n->get().borderL   = float(asset->info.borderLeft);
    n->get().borderT   = float(asset->info.borderTop);
    n->get().borderR   = float(asset->info.borderRight);
    n->get().borderB   = float(asset->info.borderBottom);
    if (n->get().type == NodeType::NinePatchPanel) {
        n->get().paddingL = float(asset->info.paddingLeft);
        n->get().paddingT = float(asset->info.paddingTop);
        n->get().paddingR = float(asset->info.paddingRight);
        n->get().paddingB = float(asset->info.paddingBottom);
    }
    return true;
}

UI::NinePatchResource *UI::loadNinePatch(const std::string &path) {
    if (auto found = ninePatches_.find(path); found != ninePatches_.end())
        return &found->second;
    auto *images = eve::ModuleManager::getInstance<eve::image::Image>("Image");
    auto *graphics = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!images || !graphics) return nullptr;
    try {
        eve::ref<eve::image::ImageData> source(images->newImageDataFromFile(path));
        NinePatchResource resource;
        std::string error;
        if (!parseNinePatch(*source, resource.info, &error)) {
            std::fprintf(stderr, "[ui] invalid .9.png '%s': %s\n", path.c_str(),
                         error.c_str());
            return nullptr;
        }
        auto cropped = stripNinePatchBorder(*source);
        if (!cropped) return nullptr;
        resource.texture = graphics->newTexture(cropped.get());
        if (!resource.texture) return nullptr;
        resource.textureId = registerTexture(resource.texture);
        if (resource.textureId == 0) {
            graphics->releaseTexture(resource.texture);
            return nullptr;
        }
        auto inserted = ninePatches_.emplace(path, std::move(resource));
        return &inserted.first->second;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[ui] failed to load .9.png '%s': %s\n", path.c_str(), e.what());
        return nullptr;
    }
}

void UI::releaseNinePatches() {
    auto *graphics = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    for (auto &[path, resource] : ninePatches_) {
        (void)path;
        if (backend_ && resource.textureId != 0)
            backend_->unregisterTexture(resource.textureId);
        if (graphics && resource.texture) graphics->releaseTexture(resource.texture);
    }
    ninePatches_.clear();
}

bool UI::requestFocus(const std::string &id) {
    if (auto host = resolveSelected()) return host->get().requestFocusById(id);
    return false;
}

bool UI::moveFocus(const std::string &direction) {
    auto host = resolveSelected();
    if (!host) return false;
    FocusDirection parsed = FocusDirection::Next;
    return parseFocusDirection(direction, &parsed) && host->get().moveFocus(parsed);
}

std::string UI::getFocusedId() const {
    if (auto host = resolveSelected()) return host->get().focusedId();
    return {};
}

void UI::setHostVisible(bool visible) {
    if (auto host = resolveSelected()) host->get().setVisible(visible);
}

void UI::setHostLayer(int layer) {
    if (auto host = resolveSelected()) host->get().setLayer(layer);
}

void UI::setHostModal(bool modal) {
    if (auto host = resolveSelected()) host->get().setModal(modal);
}

void UI::setHostOverlay(bool overlay) {
    if (auto host = resolveSelected()) host->get().meta()->overlay = overlay;
}

void UI::setHostOverlayAlpha(float alpha) {
    if (auto host = resolveSelected()) host->get().meta()->overlayBgAlpha = std::max(0.f, std::min(1.f, alpha));
}

void UI::setHostMovable(bool movable) {
    if (auto host = resolveSelected()) host->get().meta()->lockPos = !movable;
}

void UI::setHostResizable(bool resizable) {
    if (auto host = resolveSelected()) host->get().meta()->lockSize = !resizable;
}

void UI::setHostPos(float x, float y, float pivotX, float pivotY) {
    auto host = resolveSelected();
    if (!host) return;
    auto m    = host->get().meta();
    m->hasPos = true;
    m->posX = x;
    m->posY = y;
    m->pivotX = pivotX;
    m->pivotY = pivotY;
}

void UI::setHostWorldAnchor(float x, float y, float z) {
    if (auto host = resolveSelected()) host->get().setWorldAnchor(x, y, z);
}

void UI::clearHostWorldAnchor() {
    if (auto host = resolveSelected()) host->get().clearWorldAnchor();
}

void UI::setHostWorldEdgePolicy(const std::string &policy, float safeMargin) {
    if (auto host = resolveSelected()) {
        auto anchor = host->get().worldAnchor();
        anchor->edgePolicy = policy == "clamp" ? WorldAnchorEdgePolicy::Clamp
                                                : WorldAnchorEdgePolicy::Hide;
        anchor->safeMargin = std::max(0.f, safeMargin);
    }
}

void UI::setHostWorldDistanceScale(bool enabled, float referenceDistance, float minScale,
                                   float maxScale) {
    if (auto host = resolveSelected()) {
        auto anchor = host->get().worldAnchor();
        anchor->distanceScale = enabled;
        anchor->referenceDistance = std::max(referenceDistance, 0.001f);
        anchor->minScale = std::max(0.01f, std::min(minScale, maxScale));
        anchor->maxScale = std::max(anchor->minScale, std::max(minScale, maxScale));
    }
}

void UI::setHostWorldOverlap(bool enabled, int priority, float padding,
                             float maxDisplacement) {
    if (auto host = resolveSelected()) {
        auto anchor = host->get().worldAnchor();
        anchor->overlapPolicy = enabled ? WorldAnchorOverlapPolicy::Avoid
                                        : WorldAnchorOverlapPolicy::Allow;
        anchor->overlapPriority = priority;
        anchor->overlapPadding = std::max(0.f, padding);
        anchor->maxDisplacement = std::max(0.f, maxDisplacement);
    }
}

std::string UI::getHostWorldState() const {
    const auto host = resolveSelected();
    if (!host) return "disabled";
    switch (host->get().worldAnchor()->state) {
    case WorldAnchorState::Visible: return "visible";
    case WorldAnchorState::BehindCamera: return "behind-camera";
    case WorldAnchorState::OutsideViewport: return "outside-viewport";
    case WorldAnchorState::NoCamera: return "no-camera";
    case WorldAnchorState::Crowded: return "crowded";
    case WorldAnchorState::Disabled: return "disabled";
    }
    return "disabled";
}

float UI::getHostWorldScreenX() const {
    if (const auto host = resolveSelected()) return host->get().worldAnchor()->screenX;
    return 0.f;
}

float UI::getHostWorldScreenY() const {
    if (const auto host = resolveSelected()) return host->get().worldAnchor()->screenY;
    return 0.f;
}

void UI::setHostAnchor(float x, float y) {
    auto host = resolveSelected();
    if (!host) return;
    auto m     = host->get().meta();
    m->anchorX = x;
    m->anchorY = y;
}

void UI::setHostSize(float w, float h) {
    auto host = resolveSelected();
    if (!host) return;
    auto m     = host->get().meta();
    m->hasSize = true;
    m->sizeX = w;
    m->sizeY = h;
}

void UI::setHostPercent(float w, float h) {
    auto host = resolveSelected();
    if (!host) return;
    auto m      = host->get().meta();
    m->percentW = w;
    m->percentH = h;
}

void UI::animateHostPos(float x, float y, float durationMs) {
    auto host = resolveSelected();
    if (!host) return;
    auto      m = host->get().meta();
    HostTween t;
    t.host = selected_;
    t.fromX = m->hasPos ? m->posX : 0.f;
    t.fromY = m->hasPos ? m->posY : 0.f;
    t.toX = x;
    t.toY = y;
    t.startMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    t.durationMs = std::max(0.0, double(durationMs));
    m->hasPos = true;
    hostTweens_.push_back(t);
}

std::string UI::consumeClick() { return UISystem::consumeClick(); }

std::string UI::consumeChange() { return UISystem::consumeChange(); }

std::string UI::dragDropSupport() const {
    return UISystem::dragDropSupport() == DragDropSupport::Supported
               ? "supported"
               : "unsupported-platform";
}

std::string UI::consumeDrop() {
    auto drop = UISystem::consumeDrop();
    if (!drop) return {};
    lastDropType_ = std::move(drop->payloadType);
    lastDropText_ = std::move(drop->payloadText);
    lastDropSource_ = drop->sourceHostName.empty()
                          ? std::string{}
                          : drop->sourceHostName + "/" + drop->sourceNodeId;
    lastDropOrigin_ = drop->origin == DragDropOrigin::OperatingSystemFile ? "os-file"
                                                                          : "internal";
    return drop->targetHostName.empty() ? drop->targetNodeId
                                        : drop->targetHostName + "/" + drop->targetNodeId;
}

std::string UI::getDropType() const { return lastDropType_; }

std::string UI::getDropText() const { return lastDropText_; }

std::string UI::getDropSource() const { return lastDropSource_; }

std::string UI::getDropOrigin() const { return lastDropOrigin_; }

void UI::setThemeDark() { setThemeByName("dark"); }

void UI::setThemeLight() { setThemeByName("light"); }

bool UI::setTheme(const std::string &name) { return setThemeByName(name); }

std::string UI::getTheme() const { return globalThemeName(); }

std::string UI::defineStyleClass(const std::string &name, const std::string &parent) {
    return styleClassStatusName(eve::ui::defineStyleClass(name, parent));
}

void UI::animateItemOpacity(const std::string &id, float opacity, float durationMs) {
    auto host = resolveSelected();
    if (!host) return;
    auto node = host->get().findById(id);
    if (!node) return;
    itemTweens_.erase(std::remove_if(itemTweens_.begin(), itemTweens_.end(),
                                    [&](const ItemTween &t) {
                                        return t.host.table == selected_.table &&
                                               t.host.type == selected_.type &&
                                               t.host.id == selected_.id &&
                                               t.host.generation == selected_.generation &&
                                               t.nodeId == id;
                                    }),
                      itemTweens_.end());
    ItemTween tween;
    tween.host = selected_;
    tween.nodeId = id;
    tween.from = node->get().opacity;
    tween.to = std::clamp(opacity, 0.f, 1.f);
    tween.startMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    tween.durationMs = std::max(0.0, double(durationMs));
    if (tween.durationMs <= 0.0) node->get().opacity = tween.to;
    else itemTweens_.push_back(std::move(tween));
}

std::string UI::setStyleClassColor(const std::string &name, const std::string &property, float r,
                                   float g, float b, float a) {
    return styleClassStatusName(eve::ui::setStyleClassColor(name, property, r, g, b, a));
}

std::string UI::setStyleClassMetric(const std::string &name, const std::string &property, float x,
                                    float y) {
    return styleClassStatusName(eve::ui::setStyleClassMetric(name, property, x, y));
}

void UI::clearStyleClasses() { eve::ui::clearStyleClasses(); }

void UI::setNavKeyboard(bool enabled) {
    globalTheme().navEnableKeyboard = enabled;
}

void UI::setNavGamepad(bool enabled) {
    globalTheme().navEnableGamepad = enabled;
}

void UI::setScale(float scale) {
    if (!isBackendReady()) {
        if (!initBackend()) return;
    }
    if (backend_) backend_->setScale(scale);
    setThemeUiScale(getScale());
}

float UI::getScale() const {
    return backend_ ? backend_->getScale() : 1.f;
}

std::string UI::getStats() const {
    const UIStats &s = UISystem::stats();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "hosts=%d nodes=%d measureMs=%.3f walkMs=%.3f", s.hostCount,
                  s.nodeCount, s.measureMs, s.walkMs);
    return buf;
}

std::string UI::getLayoutDiagnostics() const {
    auto host = resolveSelected();
    if (!host) return "{\"nodes\":[]}";
    std::ostringstream out;
    out << "{\"host\":" << jsonQuoted(host->get().getName()) << ",\"nodes\":[";
    bool first = true;
    for (const UINode &node : host->get().tree()->nodes) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << jsonQuoted(node.id) << ",\"width\":" << node.measuredW
            << ",\"height\":" << node.measuredH << ",\"overflowX\":"
            << node.layoutOverflowX << ",\"overflowY\":" << node.layoutOverflowY << '}';
    }
    out << "]}";
    return out.str();
}

std::string UI::getAccessibilitySnapshot() const {
    auto host = resolveSelected();
    if (!host) return "{\"nodes\":[]}";
    std::ostringstream out;
    out << "{\"host\":" << jsonQuoted(host->get().getName()) << ",\"nodes\":[";
    bool first = true;
    for (const UINode &node : host->get().tree()->nodes) {
        if (node.accessibilityRole == AccessibilityRole::Auto &&
            node.accessibilityName.empty())
            continue;
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << jsonQuoted(node.id) << ",\"role\":"
            << static_cast<int>(node.accessibilityRole) << ",\"name\":"
            << jsonQuoted(node.accessibilityName) << ",\"description\":"
            << jsonQuoted(node.accessibilityDescription) << ",\"enabled\":"
            << (node.enabled ? "true" : "false") << ",\"focused\":"
            << (node.focused ? "true" : "false") << '}';
    }
    out << "]}";
    return out.str();
}

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
std::string UI::saveTreeJson() const {
    auto host = resolveSelected();
    if (!host) return "{}";
    auto               t = host->get().tree();
    Poco::JSON::Object root;
    root.set("schema", "eve.ui.tree");
    root.set("version", 4);
    root.set("host", host->get().getName());
    if (t->root >= 0) nodeToJson(*t, t->nodes[size_t(t->root)], root);
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(root, oss, 1);
    return oss.str();
}

bool UI::loadTreeJson(const std::string &json) {
    auto host = resolveSelected();
    if (!host || json.empty()) return false;
    try {
        Poco::JSON::Parser parser;
        const Poco::Dynamic::Var result = parser.parse(json);
        const Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();
        if (!obj) return false;
        if (obj->has("schema") && obj->getValue<std::string>("schema") != "eve.ui.tree")
            return false;
        const int version = obj->optValue<int>("version", 1);
        if (version < 1 || version > 4) return false;
        WidgetDesc root = descFromJson(*obj);
        host->get().setTree(std::move(root));
        return true;
    } catch (...) {
        return false;
    }
}
#else
// WebGPU trims Poco. Use EVCommon's owning Value/JSON path so UI assets retain
// their portable core rather than degrading to a silent no-op.
std::string UI::saveTreeJson() const {
    auto host = resolveSelected();
    if (!host) return "{}";
    static const char *types[] = {
        "window", "text", "button", "sameLine", "group", "separator", "checkbox",
        "slider", "progress", "inputText", "collapsingHeader", "child", "flex", "spacer",
        "image", "imageButton", "combo", "scrollList", "viewport", "searchField", "switch",
        "badge", "card", "sectionHeader", "menuBar", "menu", "menuItem", "toolbar", "toolbox",
        "sidebar", "statusBar", "splitPane", "ninePatchPanel", "colorPalette", "grid"};
    const auto tree = host->get().tree();
    std::function<eve::Value(int)> encode = [&](int index) -> eve::Value {
        const UINode &n = tree->nodes[size_t(index)];
        eve::Value node = eve::Value::object({});
        const int typeIndex = static_cast<int>(n.type);
        node.set("type", typeIndex >= 0 && typeIndex < int(std::size(types)) ? types[typeIndex]
                                                                            : "text");
        if (!n.id.empty()) node.set("id", n.id);
        if (!n.key.empty()) node.set("key", n.key);
        if (!n.text.empty()) node.set("text", n.text);
        if (!n.valueText.empty()) node.set("valueText", n.valueText);
        if (!n.tooltip.empty()) node.set("tooltip", n.tooltip);
        if (!n.styleClass.empty()) node.set("styleClass", n.styleClass);
        node.set("visible", n.visible);
        node.set("enabled", n.enabled);
        node.set("checked", n.checked);
        node.set("value", n.value);
        node.set("minValue", n.minValue);
        node.set("maxValue", n.maxValue);
        node.set("sizeX", n.sizeX);
        node.set("sizeY", n.sizeY);
        node.set("flexDirection", n.flexDirection == FlexDirection::Column ? "column" : "row");
        node.set("alignItems", int(n.alignItems));
        node.set("justifyContent", int(n.justifyContent));
        node.set("gap", n.gap);
        node.set("columnGap", n.columnGap);
        node.set("rowGap", n.rowGap);
        node.set("flexGrow", n.flexGrow);
        node.set("flexShrink", n.flexShrink);
        node.set("flexBasis", n.flexBasis);
        node.set("alignSelf", n.alignSelf);
        node.set("aspectRatio", n.aspectRatio);
        node.set("flexWrap", n.flexWrap == FlexWrap::Wrap ? "wrap" : "nowrap");
        node.set("overflow", int(n.overflow));
        node.set("gridColumns", n.gridColumns);
        node.set("gridColumnSpan", n.gridColumnSpan);
        node.set("accessibilityRole", int(n.accessibilityRole));
        node.set("accessibilityName", n.accessibilityName);
        node.set("accessibilityDescription", n.accessibilityDescription);
        eve::Value children = eve::Value::array({});
        for (int child = n.firstChild; child >= 0;
             child = tree->nodes[size_t(child)].nextSibling)
            children.pushBack(encode(child));
        node.set("children", std::move(children));
        return node;
    };
    eve::Value root = tree->root >= 0 ? encode(tree->root) : eve::Value::object({});
    root.set("schema", "eve.ui.tree");
    root.set("version", 4);
    root.set("host", host->get().getName());
    auto encoded = eve::json::stringify(root);
    return encoded.ok() ? std::move(encoded).takeValue() : "{}";
}

bool UI::loadTreeJson(const std::string &text) {
    auto host = resolveSelected();
    if (!host || text.empty()) return false;
    std::string error;
    const eve::json::Document document = eve::json::Document::parse(text, &error);
    const eve::json::Value root = document.root();
    if (!document.valid() || !root.isObject()) return false;
    if (root.has("schema") && root.getString("schema") != "eve.ui.tree") return false;
    const int version = root.getInt("version", 1);
    if (version < 1 || version > 4) return false;
    static const char *types[] = {
        "window", "text", "button", "sameLine", "group", "separator", "checkbox",
        "slider", "progress", "inputText", "collapsingHeader", "child", "flex", "spacer",
        "image", "imageButton", "combo", "scrollList", "viewport", "searchField", "switch",
        "badge", "card", "sectionHeader", "menuBar", "menu", "menuItem", "toolbar", "toolbox",
        "sidebar", "statusBar", "splitPane", "ninePatchPanel", "colorPalette", "grid"};
    std::function<WidgetDesc(eve::json::Value)> decode = [&](eve::json::Value value) {
        WidgetDesc node;
        const std::string type = value.getString("type", "text");
        for (int i = 0; i < int(std::size(types)); ++i)
            if (type == types[i]) node.type = static_cast<NodeType>(i);
        node.id = value.getString("id");
        node.key = value.getString("key");
        node.text = value.getString("text");
        node.valueText = value.getString("valueText");
        node.tooltip = value.getString("tooltip");
        node.styleClass = value.getString("styleClass");
        node.visible = value.getBool("visible", true);
        node.enabled = value.getBool("enabled", true);
        node.checked = value.getBool("checked", false);
        node.value = value.getFloat("value");
        node.minValue = value.getFloat("minValue");
        node.maxValue = value.getFloat("maxValue", 1.f);
        node.sizeX = value.getFloat("sizeX");
        node.sizeY = value.getFloat("sizeY");
        node.flexDirection = value.getString("flexDirection") == "column" ? FlexDirection::Column
                                                                           : FlexDirection::Row;
        node.alignItems = static_cast<FlexAlign>(value.getInt("alignItems"));
        node.justifyContent = static_cast<FlexJustify>(value.getInt("justifyContent"));
        node.gap = value.getFloat("gap", -1.f);
        node.columnGap = value.getFloat("columnGap", -1.f);
        node.rowGap = value.getFloat("rowGap", -1.f);
        node.flexGrow = value.getFloat("flexGrow");
        node.flexShrink = value.getFloat("flexShrink", 1.f);
        node.flexBasis = value.getFloat("flexBasis", -1.f);
        node.alignSelf = value.getInt("alignSelf", -1);
        node.aspectRatio = value.getFloat("aspectRatio");
        node.flexWrap = value.getString("flexWrap") == "wrap" ? FlexWrap::Wrap : FlexWrap::NoWrap;
        node.overflow = static_cast<OverflowMode>(value.getInt("overflow"));
        node.gridColumns = std::max(1, value.getInt("gridColumns", 1));
        node.gridColumnSpan = std::max(1, value.getInt("gridColumnSpan", 1));
        node.accessibilityRole = static_cast<AccessibilityRole>(value.getInt("accessibilityRole"));
        node.accessibilityName = value.getString("accessibilityName");
        node.accessibilityDescription = value.getString("accessibilityDescription");
        const eve::json::Value children = value.get("children");
        for (size_t i = 0; i < children.size(); ++i)
            if (children.at(i).isObject()) node.children.push_back(decode(children.at(i)));
        return node;
    };
    host->get().setTree(decode(root));
    return true;
}
#endif

graphics::Canvas *UI::viewportCanvas(const std::string &id) {
    auto host = resolveSelected();
    if (!host || id.empty()) return nullptr;
    const std::string key = host->get().getName() + "/" + id;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().canvas;
    auto ensured = UISystem::ensureViewport(key, 320, 240);
    if (!ensured.ok()) return nullptr;
    return std::move(ensured).takeValue().get().canvas;
}

bool UI::viewportHovered(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return false;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().hovered;
    return false;
}

bool UI::viewportActive(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return false;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().active;
    return false;
}

float UI::viewportMouseX(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().mouseX;
    return 0.f;
}

float UI::viewportMouseY(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().mouseY;
    return 0.f;
}

float UI::viewportDragDX(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().dragDX;
    return 0.f;
}

float UI::viewportDragDY(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().dragDY;
    return 0.f;
}

float UI::viewportWheel(const std::string &id) {
    auto host = resolveSelected();
    if (!host) return 0.f;
    if (auto state = UISystem::viewportState(host->get().getName(), id)) return state->get().wheel;
    return 0.f;
}

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
namespace {

const char *nodeTypeName(NodeType t) {
    switch (t) {
    case NodeType::Window: return "window";
    case NodeType::Text: return "text";
    case NodeType::Button: return "button";
    case NodeType::SameLine: return "sameLine";
    case NodeType::Group: return "group";
    case NodeType::Separator: return "separator";
    case NodeType::Checkbox: return "checkbox";
    case NodeType::Slider: return "slider";
    case NodeType::Progress: return "progress";
    case NodeType::InputText: return "inputText";
    case NodeType::CollapsingHeader: return "collapsingHeader";
    case NodeType::Child: return "child";
    case NodeType::Flex: return "flex";
    case NodeType::Spacer: return "spacer";
    case NodeType::Image: return "image";
    case NodeType::ImageButton: return "imageButton";
    case NodeType::Combo: return "combo";
    case NodeType::ScrollList: return "scrollList";
    case NodeType::Viewport: return "viewport";
    case NodeType::SearchField: return "searchField";
    case NodeType::Switch: return "switch";
    case NodeType::Badge: return "badge";
    case NodeType::Card: return "card";
    case NodeType::NinePatchPanel: return "ninePatchPanel";
    case NodeType::ColorPalette: return "colorPalette";
    case NodeType::Grid: return "grid";
    case NodeType::SectionHeader: return "sectionHeader";
    case NodeType::MenuBar: return "menuBar";
    case NodeType::Menu: return "menu";
    case NodeType::MenuItem: return "menuItem";
    case NodeType::Toolbar: return "toolbar";
    case NodeType::Toolbox: return "toolbox";
    case NodeType::Sidebar: return "sidebar";
    case NodeType::StatusBar: return "statusBar";
    case NodeType::SplitPane: return "splitPane";
    }
    return "text";
}

NodeType nodeTypeFromName(const std::string &s) {
    if (s == "window") return NodeType::Window;
    if (s == "button") return NodeType::Button;
    if (s == "sameLine") return NodeType::SameLine;
    if (s == "group") return NodeType::Group;
    if (s == "separator") return NodeType::Separator;
    if (s == "checkbox") return NodeType::Checkbox;
    if (s == "slider") return NodeType::Slider;
    if (s == "progress") return NodeType::Progress;
    if (s == "inputText") return NodeType::InputText;
    if (s == "collapsingHeader") return NodeType::CollapsingHeader;
    if (s == "child") return NodeType::Child;
    if (s == "flex") return NodeType::Flex;
    if (s == "spacer") return NodeType::Spacer;
    if (s == "image") return NodeType::Image;
    if (s == "imageButton") return NodeType::ImageButton;
    if (s == "combo") return NodeType::Combo;
    if (s == "scrollList") return NodeType::ScrollList;
    if (s == "viewport") return NodeType::Viewport;
    if (s == "searchField") return NodeType::SearchField;
    if (s == "switch") return NodeType::Switch;
    if (s == "badge") return NodeType::Badge;
    if (s == "card") return NodeType::Card;
    if (s == "ninePatchPanel") return NodeType::NinePatchPanel;
    if (s == "colorPalette" || s == "color-palette" || s == "color") return NodeType::ColorPalette;
    if (s == "grid") return NodeType::Grid;
    if (s == "sectionHeader") return NodeType::SectionHeader;
    if (s == "menuBar") return NodeType::MenuBar;
    if (s == "menu") return NodeType::Menu;
    if (s == "menuItem") return NodeType::MenuItem;
    if (s == "toolbar") return NodeType::Toolbar;
    if (s == "toolbox") return NodeType::Toolbox;
    if (s == "sidebar") return NodeType::Sidebar;
    if (s == "statusBar") return NodeType::StatusBar;
    if (s == "splitPane") return NodeType::SplitPane;
    return NodeType::Text;
}

void nodeToJson(const UIHost::Tree &tree, const UINode &n, Poco::JSON::Object &o) {
    o.set("type", nodeTypeName(n.type));
    if (!n.id.empty()) o.set("id", n.id);
    if (!n.key.empty()) o.set("key", n.key);
    if (!n.text.empty()) o.set("text", n.text);
    if (!n.valueText.empty()) o.set("valueText", n.valueText);
    if (!n.tooltip.empty()) o.set("tooltip", n.tooltip);
    if (!n.visible) o.set("visible", false);
    if (!n.enabled) o.set("enabled", false);
    if (n.focusMode != FocusMode::All)
        o.set("focusMode", n.focusMode == FocusMode::None ? "none" : "click");
    if (n.mouseFilter != MouseFilter::Stop)
        o.set("mouseFilter", n.mouseFilter == MouseFilter::Pass ? "pass" : "ignore");
    if (n.themePreset != ThemePreset::Inherit)
        o.set("theme", n.themePreset == ThemePreset::Dark ? "dark" : "light");
    if (!n.styleClass.empty()) o.set("styleClass", n.styleClass);
    if (n.tabIndex != 0) o.set("tabIndex", n.tabIndex);
    if (!n.focusPrevious.empty()) o.set("focusPrevious", n.focusPrevious);
    if (!n.focusNext.empty()) o.set("focusNext", n.focusNext);
    if (!n.focusLeft.empty()) o.set("focusLeft", n.focusLeft);
    if (!n.focusRight.empty()) o.set("focusRight", n.focusRight);
    if (!n.focusUp.empty()) o.set("focusUp", n.focusUp);
    if (!n.focusDown.empty()) o.set("focusDown", n.focusDown);
    if (n.accessibilityRole != AccessibilityRole::Auto) {
        const char *role = "auto";
        switch (n.accessibilityRole) {
            case AccessibilityRole::Button: role = "button"; break;
            case AccessibilityRole::Checkbox: role = "checkbox"; break;
            case AccessibilityRole::Slider: role = "slider"; break;
            case AccessibilityRole::Text: role = "text"; break;
            case AccessibilityRole::TextInput: role = "text-input"; break;
            case AccessibilityRole::List: role = "list"; break;
            case AccessibilityRole::ListItem: role = "list-item"; break;
            case AccessibilityRole::Menu: role = "menu"; break;
            case AccessibilityRole::MenuItem: role = "menu-item"; break;
            case AccessibilityRole::Progress: role = "progress"; break;
            case AccessibilityRole::Region: role = "region"; break;
            case AccessibilityRole::Tab: role = "tab"; break;
            case AccessibilityRole::Window: role = "window"; break;
            case AccessibilityRole::Auto: break;
        }
        o.set("accessibilityRole", role);
    }
    if (!n.accessibilityName.empty()) o.set("accessibilityName", n.accessibilityName);
    if (!n.accessibilityDescription.empty())
        o.set("accessibilityDescription", n.accessibilityDescription);
    if (n.dragSource) {
        o.set("dragSource", true);
        o.set("dragPayloadType", n.dragPayloadType);
        o.set("dragPayloadText", n.dragPayloadText);
    }
    if (n.dropTarget) {
        o.set("dropTarget", true);
        o.set("acceptedDropType", n.acceptedDropType);
    }
    if (n.checked) o.set("checked", true);
    if (!n.open) o.set("open", false);
    if (n.value != 0.f) o.set("value", n.value);
    if (n.minValue != 0.f) o.set("minValue", n.minValue);
    if (n.maxValue != 1.f) o.set("maxValue", n.maxValue);
    if (n.sizeX != 0.f) o.set("sizeX", n.sizeX);
    if (n.sizeY != 0.f) o.set("sizeY", n.sizeY);
    if (n.marginL != 0.f || n.marginT != 0.f || n.marginR != 0.f || n.marginB != 0.f)
        o.set("margin", Poco::Dynamic::Array({n.marginL, n.marginT, n.marginR, n.marginB}));
    if (n.paddingL != 0.f || n.paddingT != 0.f || n.paddingR != 0.f || n.paddingB != 0.f)
        o.set("padding", Poco::Dynamic::Array({n.paddingL, n.paddingT, n.paddingR, n.paddingB}));
    if (n.minSizeX != 0.f) o.set("minSizeX", n.minSizeX);
    if (n.minSizeY != 0.f) o.set("minSizeY", n.minSizeY);
    if (n.maxSizeX != 0.f) o.set("maxSizeX", n.maxSizeX);
    if (n.maxSizeY != 0.f) o.set("maxSizeY", n.maxSizeY);
    if (n.percentW != 0.f) o.set("percentW", n.percentW);
    if (n.percentH != 0.f) o.set("percentH", n.percentH);
    if (n.aspectRatio > 0.f) o.set("aspectRatio", n.aspectRatio);
    if (n.absolute) {
        o.set("absolute", true);
        o.set("anchorX", n.anchorX);
        o.set("anchorY", n.anchorY);
        o.set("posX", n.posX);
        o.set("posY", n.posY);
    }
    if (n.wrapWidth != 0.f) o.set("wrapWidth", n.wrapWidth);
    if (n.flexDirection != FlexDirection::Row)
        o.set("flexDirection", n.flexDirection == FlexDirection::Column ? "column" : "row");
    if (n.alignItems != FlexAlign::Start) o.set("alignItems", int(n.alignItems));
    if (n.justifyContent != FlexJustify::Start) o.set("justifyContent", int(n.justifyContent));
    if (n.gap >= 0.f) o.set("gap", n.gap);
    if (n.flexGrow != 0.f) o.set("flexGrow", n.flexGrow);
    if (n.flexShrink != 1.f) o.set("flexShrink", n.flexShrink);
    if (n.flexBasis >= 0.f) o.set("flexBasis", n.flexBasis);
    if (n.alignSelf >= 0) o.set("alignSelf", n.alignSelf);
    if (n.columnGap >= 0.f) o.set("columnGap", n.columnGap);
    if (n.rowGap >= 0.f) o.set("rowGap", n.rowGap);
    if (n.flexWrap == FlexWrap::Wrap) o.set("flexWrap", "wrap");
    if (n.overflow != OverflowMode::Visible) o.set("overflow", int(n.overflow));
    if (n.type == NodeType::Grid) o.set("gridColumns", n.gridColumns);
    if (n.gridColumnSpan != 1) o.set("gridColumnSpan", n.gridColumnSpan);
    if (n.type == NodeType::ColorPalette || n.tintR != 1.f || n.tintG != 1.f || n.tintB != 1.f ||
        n.tintA != 1.f)
        o.set("tint", Poco::Dynamic::Array({n.tintR, n.tintG, n.tintB, n.tintA}));
    if (n.borderL != 0.f || n.borderT != 0.f || n.borderR != 0.f || n.borderB != 0.f)
        o.set("border", Poco::Dynamic::Array({n.borderL, n.borderT, n.borderR, n.borderB}));
    if (n.cornerRadius != 0.f) o.set("cornerRadius", n.cornerRadius);
    if (n.uv0x != 0.f || n.uv0y != 0.f || n.uv1x != 1.f || n.uv1y != 1.f)
        o.set("uv", Poco::Dynamic::Array({n.uv0x, n.uv0y, n.uv1x, n.uv1y}));

    Poco::JSON::Array children;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        Poco::JSON::Object child;
        nodeToJson(tree, tree.nodes[size_t(c)], child);
        children.add(child);
    }
    if (children.size() > 0) o.set("children", children);
}

float fnum(const Poco::Dynamic::Var &v, float def = 0.f) {
    try {
        return float(v.convert<double>());
    } catch (...) {
        return def;
    }
}

void applyCommonFields(WidgetDesc &d, const Poco::JSON::Object &o) {
    if (o.has("id")) d.id = o.getValue<std::string>("id");
    if (o.has("key")) d.key = o.getValue<std::string>("key");
    if (o.has("text")) d.text = o.getValue<std::string>("text");
    if (o.has("valueText")) d.valueText = o.getValue<std::string>("valueText");
    if (o.has("tooltip")) d.tooltip = o.getValue<std::string>("tooltip");
    if (o.has("visible")) d.visible = o.getValue<bool>("visible");
    if (o.has("enabled")) d.enabled = o.getValue<bool>("enabled");
    if (o.has("focusMode"))
        d.focusMode = parseFocusMode(o.getValue<std::string>("focusMode"));
    if (o.has("mouseFilter"))
        d.mouseFilter = parseMouseFilter(o.getValue<std::string>("mouseFilter"));
    if (o.has("theme"))
        d.themePreset = parseThemePreset(o.getValue<std::string>("theme"));
    if (o.has("styleClass")) d.styleClass = o.getValue<std::string>("styleClass");
    if (o.has("tabIndex")) d.tabIndex = int(fnum(o.get("tabIndex")));
    if (o.has("focusPrevious"))
        d.focusPrevious = o.getValue<std::string>("focusPrevious");
    if (o.has("focusNext")) d.focusNext = o.getValue<std::string>("focusNext");
    if (o.has("focusLeft")) d.focusLeft = o.getValue<std::string>("focusLeft");
    if (o.has("focusRight")) d.focusRight = o.getValue<std::string>("focusRight");
    if (o.has("focusUp")) d.focusUp = o.getValue<std::string>("focusUp");
    if (o.has("focusDown")) d.focusDown = o.getValue<std::string>("focusDown");
    if (o.has("accessibilityRole"))
        d.accessibilityRole =
            parseAccessibilityRole(o.getValue<std::string>("accessibilityRole"));
    if (o.has("accessibilityName"))
        d.accessibilityName = o.getValue<std::string>("accessibilityName");
    if (o.has("accessibilityDescription"))
        d.accessibilityDescription =
            o.getValue<std::string>("accessibilityDescription");
    if (o.has("dragSource")) d.dragSource = o.getValue<bool>("dragSource");
    if (o.has("dragPayloadType"))
        d.dragPayloadType = o.getValue<std::string>("dragPayloadType");
    if (o.has("dragPayloadText"))
        d.dragPayloadText = o.getValue<std::string>("dragPayloadText");
    if (o.has("dropTarget")) d.dropTarget = o.getValue<bool>("dropTarget");
    if (o.has("acceptedDropType"))
        d.acceptedDropType = o.getValue<std::string>("acceptedDropType");
    if (o.has("checked")) d.checked = o.getValue<bool>("checked");
    if (o.has("open")) d.open = o.getValue<bool>("open");
    if (o.has("value")) d.value = fnum(o.get("value"));
    if (o.has("minValue")) d.minValue = fnum(o.get("minValue"));
    if (o.has("maxValue")) d.maxValue = fnum(o.get("maxValue"));
    if (o.has("sizeX")) d.sizeX = fnum(o.get("sizeX"));
    if (o.has("sizeY")) d.sizeY = fnum(o.get("sizeY"));
    if (o.has("margin")) {
        const Poco::JSON::Array::Ptr a = o.getArray("margin");
        if (a && a->size() >= 4) {
            d.marginL = fnum(a->get(0));
            d.marginT = fnum(a->get(1));
            d.marginR = fnum(a->get(2));
            d.marginB = fnum(a->get(3));
        }
    }
    if (o.has("padding")) {
        const Poco::JSON::Array::Ptr a = o.getArray("padding");
        if (a && a->size() >= 4) {
            d.paddingL = fnum(a->get(0));
            d.paddingT = fnum(a->get(1));
            d.paddingR = fnum(a->get(2));
            d.paddingB = fnum(a->get(3));
        }
    }
    if (o.has("minSizeX")) d.minSizeX = fnum(o.get("minSizeX"));
    if (o.has("minSizeY")) d.minSizeY = fnum(o.get("minSizeY"));
    if (o.has("maxSizeX")) d.maxSizeX = fnum(o.get("maxSizeX"));
    if (o.has("maxSizeY")) d.maxSizeY = fnum(o.get("maxSizeY"));
    if (o.has("percentW")) d.percentW = fnum(o.get("percentW"));
    if (o.has("percentH")) d.percentH = fnum(o.get("percentH"));
    if (o.has("aspectRatio")) d.aspectRatio = fnum(o.get("aspectRatio"));
    if (o.has("absolute")) d.absolute = o.getValue<bool>("absolute");
    if (o.has("anchorX")) d.anchorX = fnum(o.get("anchorX"));
    if (o.has("anchorY")) d.anchorY = fnum(o.get("anchorY"));
    if (o.has("posX")) d.posX = fnum(o.get("posX"));
    if (o.has("posY")) d.posY = fnum(o.get("posY"));
    if (o.has("wrapWidth")) d.wrapWidth = fnum(o.get("wrapWidth"));
    if (o.has("flexDirection")) d.flexDirection =
        o.getValue<std::string>("flexDirection") == "column" ? FlexDirection::Column
                                                             : FlexDirection::Row;
    if (o.has("alignItems")) d.alignItems = FlexAlign(int(fnum(o.get("alignItems"))));
    if (o.has("justifyContent"))
        d.justifyContent = FlexJustify(int(fnum(o.get("justifyContent"))));
    if (o.has("gap")) d.gap = fnum(o.get("gap"), -1.f);
    if (o.has("flexGrow")) d.flexGrow = fnum(o.get("flexGrow"));
    if (o.has("flexShrink")) d.flexShrink = fnum(o.get("flexShrink"), 1.f);
    if (o.has("flexBasis")) d.flexBasis = fnum(o.get("flexBasis"), -1.f);
    if (o.has("alignSelf")) d.alignSelf = int(fnum(o.get("alignSelf"), -1.f));
    if (o.has("columnGap")) d.columnGap = fnum(o.get("columnGap"), -1.f);
    if (o.has("rowGap")) d.rowGap = fnum(o.get("rowGap"), -1.f);
    if (o.has("flexWrap"))
        d.flexWrap = o.getValue<std::string>("flexWrap") == "wrap" ? FlexWrap::Wrap
                                                                    : FlexWrap::NoWrap;
    if (o.has("overflow")) d.overflow = OverflowMode(int(fnum(o.get("overflow"))));
    if (o.has("gridColumns"))
        d.gridColumns = std::max(1, int(fnum(o.get("gridColumns"), 1.f)));
    if (o.has("gridColumnSpan"))
        d.gridColumnSpan = std::max(1, int(fnum(o.get("gridColumnSpan"), 1.f)));
    if (o.has("tint")) {
        const Poco::JSON::Array::Ptr a = o.getArray("tint");
        if (a && a->size() >= 4) {
            d.tintR = fnum(a->get(0), 1.f);
            d.tintG = fnum(a->get(1), 1.f);
            d.tintB = fnum(a->get(2), 1.f);
            d.tintA = fnum(a->get(3), 1.f);
        }
    }
    if (o.has("border")) {
        const Poco::JSON::Array::Ptr a = o.getArray("border");
        if (a && a->size() >= 4) {
            d.borderL = fnum(a->get(0));
            d.borderT = fnum(a->get(1));
            d.borderR = fnum(a->get(2));
            d.borderB = fnum(a->get(3));
        }
    }
    if (o.has("cornerRadius")) d.cornerRadius = fnum(o.get("cornerRadius"));
    if (o.has("uv")) {
        const Poco::JSON::Array::Ptr a = o.getArray("uv");
        if (a && a->size() >= 4) {
            d.uv0x = fnum(a->get(0));
            d.uv0y = fnum(a->get(1));
            d.uv1x = fnum(a->get(2), 1.f);
            d.uv1y = fnum(a->get(3), 1.f);
        }
    }
}

WidgetDesc descFromJson(const Poco::JSON::Object &o) {
    WidgetDesc d;
    d.type = nodeTypeFromName(o.optValue<std::string>("type", "text"));
    applyCommonFields(d, o);
    if (o.has("children")) {
        const Poco::JSON::Array::Ptr children = o.getArray("children");
        for (size_t i = 0; i < children->size(); ++i) {
            const Poco::JSON::Object::Ptr child =
                children->getObject(static_cast<unsigned int>(i));
            if (child) d.children.push_back(descFromJson(*child));
        }
    }
    return d;
}

}  // namespace
#endif

void UI::mountSimple(const std::string &title, const std::string &labelText,
                     const std::string &buttonText) {
    const UIHostHandle handle =
        mountAs("default", window(title, {text(labelText, "label"), button(buttonText, "btn")}, "root"));
    if (!UIHost::resolve(handle)) return;
}

bool UI::inspectOpen() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();
    return inspector_->isOpen();
}

void UI::inspectClose() {
    if (inspector_) inspector_->close();
}

bool UI::inspectRefresh() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->refresh();
    return inspector_->instanceCount() > 0;
}

bool UI::inspectSelectClass(const std::string &name) {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();  // scans classes and mounts the panel if not open yet
    return inspector_->selectClass(name);
}

bool UI::inspectObject(ssq::Object object) {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();  // scans classes and mounts the panel if not open yet
    return inspector_->inspectObject(object);
}

bool UI::inspectSetPickHandler(ssq::Function fn) {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return false;
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, "_inspectorPickHandler", -1);
    sq_pushobject(squirrel, fn.getRaw());
    sq_newslot(squirrel, -3, SQFalse);
    sq_settop(squirrel, top);
    return true;
}

ssq::Object UI::callPickHandler() {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return {};
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return {};
    }
    sq_pushstring(squirrel, "_inspectorPickHandler", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        (sq_gettype(squirrel, -1) != OT_CLOSURE &&
         sq_gettype(squirrel, -1) != OT_NATIVECLOSURE)) {
        sq_settop(squirrel, top);
        return {};
    }
    sq_pushroottable(squirrel);  // environment
    if (SQ_FAILED(sq_call(squirrel, 1, SQTrue, SQTrue))) {
        sq_settop(squirrel, top);
        return {};
    }
    if (sq_gettype(squirrel, -1) != OT_INSTANCE) {
        sq_settop(squirrel, top);
        return {};
    }
    ssq::Object out(squirrel);
    sq_getstackobj(squirrel, -1, &out.getRaw());
    sq_addref(squirrel, &out.getRaw());
    sq_settop(squirrel, top);
    return out;
}

bool UI::inspectPickScene() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();
    const ssq::Object picked = callPickHandler();
    if (picked.getType() != ssq::Type::INSTANCE) return false;
    return inspector_->inspectObject(picked);
}

bool UI::inspectAddInstance() {
    return inspector_ && inspector_->addInstance();
}

bool UI::dbOpen() {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();
    return databasePanel_->isOpen();
}

void UI::dbClose() {
    if (databasePanel_) databasePanel_->close();
}

bool UI::dbRefresh() {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->refresh();
    return databasePanel_->isOpen();
}

bool UI::dbSelectClass(const std::string &name) {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->refresh();
    return databasePanel_->selectClass(name);
}

uint64_t UI::dbRegister(ssq::Object object, const std::string &label) {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();  // mount the panel so the entry becomes visible
    auto result = databasePanel_->registerObject(object, label);
    if (!result) return 0;
    return result.value().packed();
}

uint64_t UI::dbCreateInstance() {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();
    auto result = databasePanel_->createInstance();
    if (!result) return 0;
    return result.value().packed();
}

eve::Result<void> UI::dbUnregister(uint64_t id) {
    if (!databasePanel_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "database panel is not open", {}, {}, "ui"));
    const ObjectHandle handle = ObjectHandle::fromPacked(id);
    auto               result = databasePanel_->unregister(handle);
    if (!result.ok()) return eve::Result<void>::failure(result.status());
    return eve::Result<void>::success(result.status());
}

bool UI::editorOpen() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();
    if (!scenePanel_) scenePanel_ = std::make_unique<ScenePanel>();
    scenePanel_->setPickHandler([this](const std::string &nodeId) {
        callScenePickHandler(nodeId);
    });
    scenePanel_->open();
    if (!editorShell_) editorShell_ = std::make_unique<EditorShell>();
    editorShell_->open(inspector_->host(), databasePanel_->host(),
                       scenePanel_->host());
    return editorShell_->isOpen();
}

void UI::editorClose() {
    if (editorShell_) editorShell_->close();
}

bool UI::editorSelectPanel(const std::string &name) {
    return editorShell_ && editorShell_->selectPanel(name);
}

bool UI::sceneOpen() {
    if (!scenePanel_) scenePanel_ = std::make_unique<ScenePanel>();
    scenePanel_->setPickHandler([this](const std::string &nodeId) {
        callScenePickHandler(nodeId);
    });
    scenePanel_->open();
    return scenePanel_->isOpen();
}

void UI::sceneClose() {
    if (scenePanel_) scenePanel_->close();
}

bool UI::sceneSelectNode(const std::string &id) {
    return scenePanel_ && scenePanel_->selectNode(id);
}

bool UI::sceneSetPickHandler(ssq::Function fn) {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return false;
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, "_scenePickHandler", -1);
    sq_pushobject(squirrel, fn.getRaw());
    sq_newslot(squirrel, -3, SQFalse);
    sq_settop(squirrel, top);
    return true;
}

void UI::callScenePickHandler(const std::string &nodeId) {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return;
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return;
    }
    sq_pushstring(squirrel, "_scenePickHandler", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        (sq_gettype(squirrel, -1) != OT_CLOSURE &&
         sq_gettype(squirrel, -1) != OT_NATIVECLOSURE)) {
        sq_settop(squirrel, top);
        return;
    }
    sq_pushroottable(squirrel);                    // environment
    sq_pushstring(squirrel, nodeId.c_str(), -1);   // node id argument
    sq_call(squirrel, 2, SQFalse, SQTrue);
    sq_settop(squirrel, top);
}

void UI::expose(ssq::Table &table) {
    auto cls = table.addClass(name, UI::create, false);
    expose(cls);
    injectUIComponentClass(table);
    exposeParentScalerBindings(table);
    exposePcgColorPreviewSyncBindings(table);
    exposePcgControllerSelectionBindings(table);
    exposePcgDraggableWindowBindings(table);
    exposePcgPhotoModeApplyPlanBindings(table);
    exposePcgPhotoModePanelsBindings(table);
    exposePcgPhotoModeRuntimeUIBindings(table);
    exposePcgPhotoModeValuesBindings(table);
    exposePcgPhotoModeSessionBindings(table);
    exposePcgPhotoModeRangesBindings(table);
    exposePcgPhotoModeColorPickerBindings(table);
    exposePcgScreenshotSavedNoticeBindings(table);
    exposePcgLoadingScreenBindings(table);
    exposePcgTooltipBindings(table);
}

void UI::expose(ssq::Class &cls) {
    cls.addFunc("getName", &UI::getName);
    cls.addFunc("initBackend", &UI::initBackend);
    cls.addFunc("isBackendReady", &UI::isBackendReady);
    cls.addFunc("beginFrameAndRender", &UI::beginFrameAndRender);
    cls.addFunc("dispatchEvents", &UI::dispatchEvents);
    cls.addFunc("wantCaptureMouse", &UI::wantCaptureMouse);
    cls.addFunc("wantCaptureKeyboard", &UI::wantCaptureKeyboard);

    cls.addFunc("select", &UI::select);
    cls.addFunc("bindOwner", &UI::bindOwner);
    cls.addFunc("mountBuildAs", &UI::mountBuildAs);
    cls.addFunc("remountBuildAs", &UI::remountBuildAs);

    cls.addFunc("beginBuild", &UI::beginBuild);
    cls.addFunc("beginWindow", &UI::beginWindow);
    cls.addFunc("beginGroup", &UI::beginGroup);
    cls.addFunc("beginList", &UI::beginList);
    cls.addFunc("beginCollapsing", &UI::beginCollapsing);
    cls.addFunc("beginChild", &UI::beginChild);
    cls.addFunc("beginCard", &UI::beginCard);
    cls.addFunc("beginNinePatch", &UI::beginNinePatch);
    cls.addFunc("beginMenuBar", &UI::beginMenuBar);
    cls.addFunc("beginMenu", &UI::beginMenu);
    cls.addFunc("beginToolbar", &UI::beginToolbar);
    cls.addFunc("beginToolbox", &UI::beginToolbox);
    cls.addFunc("beginSidebar", &UI::beginSidebar);
    cls.addFunc("beginStatusBar", &UI::beginStatusBar);
    cls.addFunc("beginSplitPane", &UI::beginSplitPane);
    cls.addFunc("beginScrollList", &UI::beginScrollList);
    cls.addFunc("beginFlex", &UI::beginFlex);
    cls.addFunc("beginRow", &UI::beginRow);
    cls.addFunc("beginColumn", &UI::beginColumn);
    cls.addFunc("beginGrid", &UI::beginGrid);
    cls.addFunc("end", &UI::end);
    cls.addFunc("text", &UI::addText);
    cls.addFunc("textWrapped", &UI::addTextWrapped);
    cls.addFunc("button", &UI::addButton);
    cls.addFunc("icon", &UI::addIcon);
    cls.addFunc("iconButton", &UI::addIconButton);
    cls.addFunc("sameLine", &UI::addSameLine);
    cls.addFunc("separator", &UI::addSeparator);
    cls.addFunc("checkbox", &UI::addCheckbox);
    cls.addFunc("slider", &UI::addSlider);
    cls.addFunc("colorPalette", &UI::addColorPalette);
    cls.addFunc("progress", &UI::addProgress);
    cls.addFunc("image", &UI::addImage);
    cls.addFunc("ninePatch", &UI::addNinePatch);
    cls.addFunc("imageButton", &UI::addImageButton);
    cls.addFunc("viewport", &UI::addViewport);
    cls.addFunc("combo", &UI::addCombo);
    cls.addFunc("inputText", &UI::addInputText);
    cls.addFunc("searchField", &UI::addSearchField);
    cls.addFunc("switch", &UI::addSwitch);
    cls.addFunc("badge", &UI::addBadge);
    cls.addFunc("sectionHeader", &UI::addSectionHeader);
    cls.addFunc("menuItem", &UI::addMenuItem);
    cls.addFunc("spacer", &UI::addSpacer);
    cls.addFunc("setItemFlexGrow", &UI::setItemFlexGrow);
    cls.addFunc("setItemFlexShrink", &UI::setItemFlexShrink);
    cls.addFunc("setItemFlexBasis", &UI::setItemFlexBasis);
    cls.addFunc("setItemAlignSelf", &UI::setItemAlignSelf);
    cls.addFunc("setItemAspectRatio", &UI::setItemAspectRatio);
    cls.addFunc("setItemGridColumnSpan", &UI::setItemGridColumnSpan);
    cls.addFunc("setItemSize", &UI::setItemSize);
    cls.addFunc("setItemMargin", &UI::setItemMargin);
    cls.addFunc("setItemPadding", &UI::setItemPadding);
    cls.addFunc("setItemMinSize", &UI::setItemMinSize);
    cls.addFunc("setItemMaxSize", &UI::setItemMaxSize);
    cls.addFunc("setItemPercent", &UI::setItemPercent);
    cls.addFunc("setItemAbsolute", &UI::setItemAbsolute);
    cls.addFunc("setItemTooltip", &UI::setItemTooltip);
    cls.addFunc("setItemDragSource", &UI::setItemDragSource);
    cls.addFunc("setItemDropTarget", &UI::setItemDropTarget);
    cls.addFunc("setItemEnabled", &UI::setItemEnabled);
    cls.addFunc("setItemSelected", &UI::setItemSelected);
    cls.addFunc("setItemFocusMode", &UI::setItemFocusMode);
    cls.addFunc("setItemMouseFilter", &UI::setItemMouseFilter);
    cls.addFunc("setItemTheme", &UI::setItemTheme);
    cls.addFunc("setThemeScope", &UI::setThemeScope);
    cls.addFunc("setItemStyleClass", &UI::setItemStyleClass);
    cls.addFunc("setStyleScope", &UI::setStyleScope);
    cls.addFunc("setItemTabIndex", &UI::setItemTabIndex);
    cls.addFunc("setItemFocusOrder", &UI::setItemFocusOrder);
    cls.addFunc("setItemFocusNeighbors", &UI::setItemFocusNeighbors);
    cls.addFunc("setItemAccessibility", &UI::setItemAccessibility);
    cls.addFunc("setFlexAlign", &UI::setFlexAlign);
    cls.addFunc("setFlexJustify", &UI::setFlexJustify);
    cls.addFunc("setLayoutGaps", &UI::setLayoutGaps);
    cls.addFunc("setFlexWrap", &UI::setFlexWrap);
    cls.addFunc("setLayoutOverflow", &UI::setLayoutOverflow);
    cls.addFunc("listItem", &UI::addListItem);
    cls.addFunc("mountBuild", &UI::mountBuild);

    cls.addFunc("setText", &UI::setText);
    cls.addFunc("setTextWrap", &UI::setTextWrap);
    cls.addFunc("setVisible", &UI::setVisible);
    cls.addFunc("setEnabled", &UI::setEnabled);
    cls.addFunc("setChecked", &UI::setChecked);
    cls.addFunc("setValue", &UI::setValue);
    cls.addFunc("setValueText", &UI::setValueText);
    cls.addFunc("setColor", &UI::setColor);
    cls.addFunc("setImageTint", &UI::setImageTint);
    cls.addFunc("setImageUv", &UI::setImageUv);
    cls.addFunc("setImageNinePatch", &UI::setImageNinePatch);
    cls.addFunc("setImageNinePatchFile", &UI::setImageNinePatchFile);
    cls.addFunc("setImageCornerRadius", &UI::setImageCornerRadius);
    cls.addFunc("setImageTextureId", &UI::setImageTextureId);
    cls.addFunc("registerTexture", &UI::registerTexture);
    cls.addFunc("unregisterTexture", &UI::unregisterTexture);
    cls.addFunc("getValue", &UI::getValue);
    cls.addFunc("getValueText", &UI::getValueText);
    cls.addFunc("getColorR", &UI::getColorR);
    cls.addFunc("getColorG", &UI::getColorG);
    cls.addFunc("getColorB", &UI::getColorB);
    cls.addFunc("getColorA", &UI::getColorA);
    cls.addFunc("getChecked", &UI::getChecked);
    cls.addFunc("requestFocus", &UI::requestFocus);
    cls.addFunc("moveFocus", &UI::moveFocus);
    cls.addFunc("getFocusedId", &UI::getFocusedId);
    cls.addFunc("setHostVisible", &UI::setHostVisible);
    cls.addFunc("setHostLayer", &UI::setHostLayer);
    cls.addFunc("setHostModal", &UI::setHostModal);
    cls.addFunc("setHostOverlay", &UI::setHostOverlay);
    cls.addFunc("setHostOverlayAlpha", &UI::setHostOverlayAlpha);
    cls.addFunc("setHostMovable", &UI::setHostMovable);
    cls.addFunc("setHostResizable", &UI::setHostResizable);
    cls.addFunc("setHostPos", &UI::setHostPos);
    cls.addFunc("setHostWorldAnchor", &UI::setHostWorldAnchor);
    cls.addFunc("clearHostWorldAnchor", &UI::clearHostWorldAnchor);
    cls.addFunc("setHostWorldEdgePolicy", &UI::setHostWorldEdgePolicy);
    cls.addFunc("setHostWorldDistanceScale", &UI::setHostWorldDistanceScale);
    cls.addFunc("setHostWorldOverlap", &UI::setHostWorldOverlap);
    cls.addFunc("getHostWorldState", &UI::getHostWorldState);
    cls.addFunc("getHostWorldScreenX", &UI::getHostWorldScreenX);
    cls.addFunc("getHostWorldScreenY", &UI::getHostWorldScreenY);
    cls.addFunc("setHostAnchor", &UI::setHostAnchor);
    cls.addFunc("setHostSize", &UI::setHostSize);
    cls.addFunc("setHostPercent", &UI::setHostPercent);
    cls.addFunc("animateHostPos", &UI::animateHostPos);
    cls.addFunc("animateItemOpacity", &UI::animateItemOpacity);
    cls.addFunc("consumeClick", &UI::consumeClick);
    cls.addFunc("consumeChange", &UI::consumeChange);
    cls.addFunc("dragDropSupport", &UI::dragDropSupport);
    cls.addFunc("consumeDrop", &UI::consumeDrop);
    cls.addFunc("getDropType", &UI::getDropType);
    cls.addFunc("getDropText", &UI::getDropText);
    cls.addFunc("getDropSource", &UI::getDropSource);
    cls.addFunc("getDropOrigin", &UI::getDropOrigin);
    cls.addFunc("onClick", &UI::onClick);
    cls.addFunc("onChange", &UI::onChange);
    cls.addFunc("componentOnClick", &UI::componentOnClick);
    cls.addFunc("componentOnChange", &UI::componentOnChange);
    cls.addFunc("componentClearHandlers", &UI::componentClearHandlers);

    cls.addFunc("setThemeDark", &UI::setThemeDark);
    cls.addFunc("setThemeLight", &UI::setThemeLight);
    cls.addFunc("setTheme", &UI::setTheme);
    cls.addFunc("defineStyleClass", &UI::defineStyleClass);
    cls.addFunc("setStyleClassColor", &UI::setStyleClassColor);
    cls.addFunc("setStyleClassMetric", &UI::setStyleClassMetric);
    cls.addFunc("clearStyleClasses", &UI::clearStyleClasses);
    cls.addFunc("getTheme", &UI::getTheme);
    cls.addFunc("setNavKeyboard", &UI::setNavKeyboard);
    cls.addFunc("setNavGamepad", &UI::setNavGamepad);
    cls.addFunc("setScale", &UI::setScale);
    cls.addFunc("getScale", &UI::getScale);
    cls.addFunc("getStats", &UI::getStats);
    cls.addFunc("getLayoutDiagnostics", &UI::getLayoutDiagnostics);
    cls.addFunc("getAccessibilitySnapshot", &UI::getAccessibilitySnapshot);
    cls.addFunc("saveTreeJson", &UI::saveTreeJson);
    cls.addFunc("loadTreeJson", &UI::loadTreeJson);
    cls.addFunc("viewportCanvas", &UI::viewportCanvas);
    cls.addFunc("viewportHovered", &UI::viewportHovered);
    cls.addFunc("viewportActive", &UI::viewportActive);
    cls.addFunc("viewportMouseX", &UI::viewportMouseX);
    cls.addFunc("viewportMouseY", &UI::viewportMouseY);
    cls.addFunc("viewportDragDX", &UI::viewportDragDX);
    cls.addFunc("viewportDragDY", &UI::viewportDragDY);
    cls.addFunc("viewportWheel", &UI::viewportWheel);

    cls.addFunc("mountSimple", &UI::mountSimple);

    cls.addFunc("inspect", &UI::inspectOpen);
    cls.addFunc("inspectClose", &UI::inspectClose);
    cls.addFunc("inspectRefresh", &UI::inspectRefresh);
    cls.addFunc("inspectSelectClass", &UI::inspectSelectClass);
    cls.addFunc("inspectObject", &UI::inspectObject);
    cls.addFunc("inspectSetPickHandler", &UI::inspectSetPickHandler);
    cls.addFunc("inspectPickScene", &UI::inspectPickScene);
    cls.addFunc("inspectAddInstance", &UI::inspectAddInstance);

    cls.addFunc("dbOpen", &UI::dbOpen);
    cls.addFunc("dbClose", &UI::dbClose);
    cls.addFunc("dbRefresh", &UI::dbRefresh);
    cls.addFunc("dbSelectClass", &UI::dbSelectClass);
    cls.addFunc("dbRegister", &UI::dbRegister);
    cls.addFunc("dbCreateInstance", &UI::dbCreateInstance);
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("dbUnregister", [vm](UI *value, uint64_t id) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "UI instance must not be null", {}, {}, "ui")));
        return eve::script::projectResult(vm, value->dbUnregister(id));
    });

    cls.addFunc("editorOpen", &UI::editorOpen);
    cls.addFunc("editorClose", &UI::editorClose);
    cls.addFunc("editorSelectPanel", &UI::editorSelectPanel);

    cls.addFunc("sceneOpen", &UI::sceneOpen);
    cls.addFunc("sceneClose", &UI::sceneClose);
    cls.addFunc("sceneSelectNode", &UI::sceneSelectNode);
    cls.addFunc("sceneSetPickHandler", &UI::sceneSetPickHandler);
}

}  // namespace eve::ui
