// Sprite stacking pseudo-3D demo.
//
// A 3D primitive is sliced into thin RGBA layers on the CPU, then rendered as
// a stack of alpha-blended slices inside the 3D forward pass. From the design
// view angle the stack reads as a solid 3D object; rotating the camera (or the
// stack yaw) reveals the classic pseudo-3D parallax.
//
// Controls:
//   A / D    rotate stack yaw
//   W / S    camera distance
//   H        toggle vertical (billboard) / horizontal (top-down) mode
//   1..4     primitive: cylinder / sphere / cone / box
//   M        toggle procedural primitive / rock.obj (model-file slicing)
//   Q / E    slice thickness
//   R        rebuild (reslice) the stack

if (!("stackYaw" in getroottable())) stackYaw <- 0.0;
if (!("camDist" in getroottable())) camDist <- 6.0;
if (!("stackMode" in getroottable())) stackMode <- "vertical";
if (!("stackKind" in getroottable())) stackKind <- "cylinder";
if (!("sliceCount" in getroottable())) sliceCount <- 20;
if (!("sliceThick" in getroottable())) sliceThick <- 0.0;
if (!("stack" in getroottable())) stack <- null;
if (!("batch" in getroottable())) batch <- null;
if (!("satellites" in getroottable())) satellites <- [];
if (!("rockLayers" in getroottable())) rockLayers <- null;
if (!("useModel" in getroottable())) useModel <- false;
if (!("prevKeys" in getroottable())) prevKeys <- {};

local tints = {
    cylinder = [0.35, 0.78, 0.72],
    sphere   = [0.85, 0.52, 0.28],
    cone     = [0.55, 0.42, 0.30],
    box      = [0.42, 0.55, 0.85],
};

function pressed(k) {
    local down = keyboard.isDown(k);
    local old = k in prevKeys ? prevKeys[k] : false;
    prevKeys[k] <- down;
    return down && !old;
}

function rebuildStack() {
    local axis = stackMode == "vertical" ? "z" : "y";
    if (useModel) {
        local md = model3d.newModelDataFromFile("assets/rock.obj");
        rockLayers = spritestack.sliceModel(md, 22, 128, 128, axis, 0.0);
    } else {
        rockLayers = null;
    }
    local layers = rockLayers != null
        ? rockLayers
        : spritestack.slicePrimitive(stackKind, sliceCount, 128, 128, axis, 0.0);
    stack = spritestack.newStack(gfx);
    stack.setLayerCount(layers.len());
    for (local i = 0; i < layers.len(); i++)
        stack.setLayerImage(gfx, layers[i], i);
    stack.setThickness(stackMode == "vertical" ? 0.12 : 0.17);
    stack.setSize(2.4, 2.8);
    stack.setPosition(0.0, 0.0, 0.0);
    stack.setMode(stackMode);
    local t = useModel ? [0.55, 0.48, 0.40] : tints[stackKind];
    stack.setTint(t[0], t[1], t[2], 1.0);
    stack.setShadowEnabled(true);
    stack.setShadowOpacity(0.38);
    stack.setShadowLight(-0.45, -1.0, -0.35);
    stack.setShadowPlaneY(-1.53);
    stack.setCastShadow(true);  // real CSM shadow from the slice silhouettes
    if (stackMode == "vertical") stack.setOutline(0.045, 0.02, 0.03, 0.04);
    rebuildSatellites();
}

// A few small copies sharing the main stack's layer textures, drawn in a
// single batched draw call per (texture, tint) group.
function rebuildSatellites() {
    if (batch == null) batch = spritestack.newBatch(gfx);
    batch.clear();
    satellites = [];
    local offsets = [[2.6, 0.9], [-2.5, 1.0], [3.0, -1.2]];
    foreach (off in offsets) {
        local s = spritestack.newStack(gfx);
        s.setLayerCount(stack.getLayerCount());
        for (local i = 0; i < stack.getLayerCount(); i++)
            s.setLayerTexture(stack.getLayerTexture(i), i);
        s.setThickness(stack.getThickness());
        s.setSize(0.85, 0.95);
        s.setPosition(off[0], 0.0, off[1]);
        s.setMode(stackMode);
        s.setTint(0.58, 0.52, 0.46, 1.0);
        batch.add(s);
        satellites.append(s);
    }
}

local cam = eve.Camera3D();
cam.setEye(0.0, 2.8, camDist);
cam.setTarget(0.0, 0.2, 0.0);
cam.setUp(0.0, 1.0, 0.0);
cam.setFov(45.0);
cam.setAmbient(0.32, 0.35, 0.38);
cam.setActive(true);
gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.25, 1.18, 1.05);
gfx.setBackgroundColor(0.075, 0.10, 0.12, 1.0);

// Flat ground disc so the stack's depth reads against the scene.
local ground = eve.Renderable3D();
ground.setMesh(gfx.newMeshCylinder(48, 1, true));
ground.setPosition(0.0, -1.55, 0.0);
ground.setScale(9.0, 0.18, 9.0);
ground.setTint(0.20, 0.30, 0.24, 1.0);
ground.setRoughness(0.9);

rebuildStack();

function eve_update(dt) {
    if (pressed("h") || pressed("H")) {
        stackMode = stackMode == "vertical" ? "horizontal" : "vertical";
        rebuildStack();
    }
    if (pressed("r") || pressed("R")) rebuildStack();
    if (pressed("1")) { stackKind = "cylinder"; rebuildStack(); }
    if (pressed("2")) { stackKind = "sphere"; rebuildStack(); }
    if (pressed("3")) { stackKind = "cone"; rebuildStack(); }
    if (pressed("4")) { stackKind = "box"; rebuildStack(); }
    if (pressed("m") || pressed("M")) { useModel = !useModel; rebuildStack(); }
    if (keyboard.isDown("a") || keyboard.isDown("A")) stackYaw -= dt * 0.9;
    if (keyboard.isDown("d") || keyboard.isDown("D")) stackYaw += dt * 0.9;
    if (keyboard.isDown("w") || keyboard.isDown("W")) camDist = max(2.5, camDist - dt * 3.0);
    if (keyboard.isDown("s") || keyboard.isDown("S")) camDist = min(14.0, camDist + dt * 3.0);
    stack.setYaw(stackYaw);
    cam.setEye(0.0, 2.8, camDist);
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    // Draw the pseudo-3D stack into the open 3D pass (before present).
    stack.render(gfx);
    batch.render(gfx);
}
