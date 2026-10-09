// Workspace recipe: Scene outline + inspector + gizmo
// Full reference: examples/scene-editor (+ composable-editor for schema-driven panels)
// Object model: docs/dev/场景对象模型使用指南.md
//
// Composition order:
//   1. Mount a SceneHost; keep transforms on SceneNode
//   2. Attach eve.SceneEntity only for scripted behaviour
//   3. Cross-domain objects (mesh / body / camera) via Link, not inheritance
//   4. Wire editor Dock + Inspector + Gizmo to the same selection channel
//
// Minimal live smoke of the object-model cookbook:

persist demo = { scene = null, ticks = 0 };

class Pulse extends eve.SceneEntity {
    function onAttach() { print("[workspace-recipe] SceneEntity attached on " + nodeId); }
    function update(dt) {
        demo.ticks += 1;
        if (demo.ticks == 1) {
            // Safe detach from inside update — drained at end of scene.update.
            getScene().scheduleDetachEntity(nodeId, this);
        }
    }
    function onDetach() { print("[workspace-recipe] SceneEntity detached"); }
}

function eve_init() {
    demo.scene = eve.Scene();
    demo.scene.beginBuild();
    demo.scene.beginNode("root");
    demo.scene.addNode("marker");
    demo.scene.end();
    demo.scene.mountBuildAs("recipe");
    local e = demo.scene.attachEntity("marker", Pulse);
    if (e == null) {
        print("[workspace-recipe] scene-inspect: attachEntity failed");
        return;
    }
    e.disable();
    e.enable();
    print("[workspace-recipe] scene-inspect: Node + SceneEntity + scheduleDetach ready");
    print("[workspace-recipe] full editor: examples/scene-editor");
}

function eve_update(dt) {
    if (demo.scene != null) demo.scene.update(dt);
}

function eve_render() {}
