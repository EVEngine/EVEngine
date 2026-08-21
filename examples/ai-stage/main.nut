// AI Stage — an empty 3D stage that hosts the `scene_director` authoring kit.
//
// Purpose: a neutral, always-ready scene the agents build into over MCP.
//   - story-scene-agent / creative-brain  place props via eve_scene_modify
//   - scene-qc / RenderVision  frame cameras and screenshot via MCP
// Run with DevTools + MCP so agents can connect:
//   eve run --debug --mcp-port=7529 examples/ai-stage

// Install the embedded scene-director kit (idempotent). `sceneDirectorScript`
// is baked into the engine from src/scripts/scene_director.nut.
if ("sceneDirectorScript" in eve && eve.sceneDirectorScript != "") {
    try {
        local fn = compilestring(eve.sceneDirectorScript);
        if (fn != null) fn();
        else print("scene_director install failed: compilestring returned null\n");
    } catch (e) {
        print("scene_director install failed: " + e + "\n");
    }
} else if (file_exists("scene_director.nut")) {
    try {
        dofile("scene_director.nut");
    } catch (e) {
        print("scene_director file load failed: " + e + "\n");
    }
}

if (!("scene_director" in getroottable())) scene_director <- null;
if (!("sd" in getroottable())) sd <- null;
if (scene_director != null) sd = scene_director;

// Minimal default stage so the window is never empty before an agent builds.
if (sd != null) {
    sd.reset();
    sd.spawn({
        id = "terrain", kind = "ground",
        pos = [0.0, 0.0, 0.0],
        scale = [40.0, 1.0, 40.0],
        tint = [0.40, 0.45, 0.42],
        roughness = 0.95,
    });
    sd.set_lighting({ timeOfDay = "day", atmosphere = "clear", intensity = 1.0 });
    sd.camera({ eye = [0.0, 10.0, 20.0], target = [0.0, 1.0, 0.0], fov = 55.0 });
} else {
    gfx.setBackgroundColor(0.10, 0.13, 0.19, 1.0);
}

function eve_update(dt) {
    // Idle host: agents drive the scene over MCP (eve_scene_modify / camera /
    // screenshot). Nothing to tick here.
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
