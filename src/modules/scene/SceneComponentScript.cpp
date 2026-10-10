#include <cstring>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::scene::detail {
namespace {
/** Script base: `class X extends eve.SceneComponent { function build() { ... } }`. */
const char *kSceneComponentScript = R"SQ(
if (!("_sceneComponents" in eve)) eve._sceneComponents <- []
if (!("_flushingSceneComponents" in eve)) eve._flushingSceneComponents <- false
eve._scheduleSceneComponent <- function(component) {
    if (component._scheduled) return
    component._scheduled = true
    eve._sceneComponents.append(component)
}
::eve_scene_flush_components <- function() {
    if (eve._flushingSceneComponents) return
    eve._flushingSceneComponents = true
    local pending = eve._sceneComponents
    eve._sceneComponents = []
    for (local i = 0; i < pending.len(); i += 1) {
        local component = pending[i]
        component._scheduled = false
        try {
            if (component._mounted && component.dirty) component.updateIfDirty()
        } catch (error) {
            for (local j = i + 1; j < pending.len(); j += 1) eve._sceneComponents.append(pending[j])
            eve._flushingSceneComponents = false
            throw error
        }
    }
    eve._flushingSceneComponents = false
}
eve.SceneComponent <- class {
    hostName = ""
    dirty = true
    forceFull = false
    _scene = null
    _mounted = false
    _scheduled = false

    constructor(sceneInstance = null) {
        _scene = sceneInstance
        hostName = ""
        dirty = true
        forceFull = false
        _mounted = false
    }

    function setScene(sceneInstance) { _scene = sceneInstance }

    function mountAs(name) {
        hostName = name
        dirty = true
        forceFull = true
        updateIfDirty()
    }

    function setState() { markDirty() }
    function markDirty() {
        dirty = true
        if (_mounted) eve._scheduleSceneComponent(this)
    }
    function onMount() {}

    // Override: call this.scene().beginNode / addNode / end ...
    function build() {}

    function scene() {
        if (_scene != null) return _scene
        try {
            if (::scene != null) return ::scene
        } catch (e) {}
        _scene = ::eve.Scene()
        return _scene
    }

    function updateIfDirty() {
        if (!dirty) return false
        local s = this.scene()
        dirty = false
        _scheduled = false
        s.beginBuild()
        try {
        this.build()
        local name = hostName
        if (name == null || name == "") name = "default"
        if (forceFull) {
            s.mountBuildAs(name)
            forceFull = false
        } else {
            s.remountBuildAs(name)
        }
        if (!_mounted) {
            _mounted = true
            onMount()
        }
        if (dirty) eve._scheduleSceneComponent(this)
        } catch (error) { markDirty(); throw error }
        return true
    }

    function rebuild(force = false) {
        dirty = true
        forceFull = force
        return updateIfDirty()
    }
}
)SQ";

}  // namespace

void injectSceneComponentClass(ssq::Table &eveTable) {
    HSQUIRRELVM     vm  = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kSceneComponentScript,
                                   static_cast<SQInteger>(std::strlen(kSceneComponentScript)), "SceneComponent.nut",
                                   SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

}  // namespace eve::scene::detail
