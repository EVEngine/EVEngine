// SceneLoader is a manager-owned singleton. Each generation needs its own key:
// loading the same key twice would overwrite its ownership record.
persist blenderLoader = null
persist blenderCamera = null
persist blenderActive = ""
persist blenderPending = true
persist blenderPoll = 0.0
persist blenderFailed = ""

function blenderRefresh() {
    local next = fs.readText("current-v1.txt");
    if (next == "") next = "scene.glb";
    // The v1 manifest is exactly one relative basename, without a newline.
    if (next != "scene.glb") {
        if (next.len() != 42 || next.slice(0, 6) != "scene-" ||
            next.slice(38) != ".glb") return;
        for (local i = 6; i < 38; ++i) {
            local c = next.slice(i, i + 1);
            if ("0123456789abcdef".find(c) == null) return;
        }
    }
    if (next == blenderActive) return;
    local candidate = blenderLoader.load(next);
    if (candidate == null) {
        if (blenderFailed != next)
            print("[blender] load failed; keeping previous scene: " + next + "\n");
        blenderFailed = next;
        return;
    }
    // No render/update callback runs between mount and retirement. Full replace,
    // with the previous scene retained on decode failure; no SceneDiff involved.
    local previous = blenderActive;
    blenderActive = next;
    if (previous != "") blenderLoader.unload(previous);
    blenderFailed = "";
    print("[blender] replaced scene: " + next + "\n");
}

eve_init = function() {
    blenderLoader = eve.SceneLoader();
    blenderCamera = eve.Camera3D();
    blenderCamera.setEye(8.0, 6.0, 10.0);
    blenderCamera.setTarget(0.0, 0.8, 0.0);
    blenderCamera.setUp(0.0, 1.0, 0.0);
    blenderCamera.setFov(45.0);
    blenderCamera.setAmbient(0.35, 0.35, 0.35);
    blenderCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.3, 1.0, 0.95, 0.85);
    gfx.setBackgroundColor(0.08, 0.10, 0.14, 1.0);
    blenderRefresh();
};

eve_asset_reload <- function(path) {
    if (path_endswith(path, "current-v1.txt")) blenderPending = true;
};

eve_update = function(dt) {
    blenderPoll += dt;
    // Reconcile the tiny manifest every 250 ms as well: missing/coalesced native
    // events and a temporarily unreadable file must not require another save.
    if (blenderPending || blenderPoll >= 0.25) {
        blenderPending = false;
        blenderPoll = 0.0;
        blenderRefresh();
    }
    scene.update(dt);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};

eve_quit = function() {
    if (blenderActive != "") blenderLoader.unload(blenderActive);
    blenderActive = "";
};
