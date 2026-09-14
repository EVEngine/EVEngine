// Level designer entry point.
//
// Edit mode authors the level through the SceneEditorSession command registry, so
// undo/redo and conflict detection behave exactly as in examples/scene-editor.
// Play mode (F5) builds a runtime world from the authored data and drives a
// Motion Matching character; it never mutates authored state.
//
// UI is a classic editor workspace (toolbar / outliner / viewport / inspector /
// asset palette) — see level_ui.nut.

dofile("level_state.nut");
dofile("level_json.nut");
dofile("level_terrain.nut");
dofile("level_pick.nut");
dofile("level_characters.nut");
dofile("level_whitebox.nut");
dofile("level_spawn.nut");
dofile("level_play.nut");
dofile("level_document.nut");
dofile("level_ui.nut");

function levelBuildSceneHost() {
    scene.beginBuild();
    scene.beginNode("root", "World");
    scene.end();
    scene.mountBuildAs(levelHost());
}

function levelSetupCamera() {
    level.camera = eve.Camera3D();
    level.camera.setFov(52.0);
    // The engine default far plane is 100 m, which clips a level-sized terrain,
    // so the designer owns its own range and keeps the near plane tight enough
    // for gizmo and surface picking.
    level.camera.setClipPlanes(0.15, 4000.0);
    level.camera.setActive(true);
    level.camera.setAmbient(0.30, 0.33, 0.38);
    levelUpdateOrbitCamera();
}

function levelUpdateOrbitCamera() {
    local focusX = level.focusX, focusY = level.focusY, focusZ = level.focusZ;
    if (level.mode == "play" && level.play != null) {
        focusX = level.play.focusX;
        focusY = level.play.focusY;
        focusZ = level.play.focusZ;
    }
    level.camera.setEye(focusX + level.distance * cos(level.pitch) * sin(level.yaw),
        focusY + level.distance * sin(level.pitch),
        focusZ + level.distance * cos(level.pitch) * cos(level.yaw));
    level.camera.setTarget(focusX, focusY, focusZ);
}

/**
 * @brief Ground-plane basis of the current orbit view.
 *
 * Matches the eye offset above: the view direction on the ground is
 * `forward`, and `right = cross(forward, up)`. Panning uses these so the world
 * tracks the cursor at any yaw.
 */
function levelCameraGroundBasis() {
    local forwardX = -sin(level.yaw), forwardZ = -cos(level.yaw);
    return {forwardX = forwardX, forwardZ = forwardZ,
            rightX = cos(level.yaw), rightZ = -sin(level.yaw)};
}

/** @brief Frame the current selection, or the whole terrain when nothing is selected. */
function levelFrameSelection() {
    local object = levelObject(level.selected);
    if (object == null) { levelFrameTerrain(); return; }
    local world = scene.localToWorldAt(levelHost(), object.id, 0.0, 0.0, 0.0);
    level.focusX = world[0];
    level.focusY = world[1];
    level.focusZ = world[2];
    local t = object.transform;
    local scale = t.scaleX > t.scaleY ? t.scaleX : t.scaleY;
    if (scale < t.scaleZ) scale = t.scaleZ;
    level.distance = levelClamp(14.0 * (scale > 0.0 ? scale : 1.0), 3.0, 3000.0);
    levelUpdateOrbitCamera();
    level.status = "focused " + object.name;
}

/**
 * @brief Pan the editor focus by a cursor delta, in the view's ground basis.
 *
 * Dragging right moves the focus left so the world tracks the cursor; dragging
 * down moves the focus away along the view direction. The step scales with the
 * orbit distance, so panning feels identical however far out the camera is.
 */
function levelPanCamera(deltaX, deltaY) {
    local basis = levelCameraGroundBasis();
    local scale = level.distance * 0.0016;
    level.focusX += (basis.forwardX * deltaY - basis.rightX * deltaX) * scale;
    level.focusZ += (basis.forwardZ * deltaY - basis.rightZ * deltaX) * scale;
    levelUpdateOrbitCamera();
}

function levelFrameTerrain() {
    if (level.terrain == null) return;
    local extent = terrainWorldExtent(level.terrain);
    local spanX = extent.maxX - extent.minX;
    local spanZ = extent.maxZ - extent.minZ;
    level.focusX = (extent.minX + extent.maxX) * 0.5;
    level.focusY = 0.0;
    level.focusZ = (extent.minZ + extent.maxZ) * 0.5;
    level.distance = 0.95 * (spanX > spanZ ? spanX : spanZ);
    level.pitch = 0.72;
    level.yaw = 0.85;
    levelUpdateOrbitCamera();
}

/** @brief Live gizmo preview for the selected piece/spawn marker. */
function levelPreviewSelection() {
    local object = levelObject(level.selected);
    if (object == null || !level.gizmo.isDragging()) return;
    local preview = {
        id = level.selected,
        transform = {
            x = level.gizmo.getPositionX(), y = level.gizmo.getPositionY(), z = level.gizmo.getPositionZ(),
            rotationX = level.gizmo.getRotationX(), rotationY = level.gizmo.getRotationY(),
            rotationZ = level.gizmo.getRotationZ(),
            scaleX = level.gizmo.getScaleX(), scaleY = level.gizmo.getScaleY(), scaleZ = level.gizmo.getScaleZ()
        }
    };
    if (level.selected in level.pieces) whiteboxApplyTransform(level.pieces[level.selected], preview);
    if (level.selected in level.spawns) spawnApplyTransform(level.spawns[level.selected], preview);
}

/** @brief Parent-local gizmo point → world, for overlay drawing. */
function levelGizmoWorldPoint(object, lx, ly, lz) {
    if (object == null || object.parent == "") return [lx, ly, lz];
    return scene.localToWorldAt(levelHost(), object.parent, lx, ly, lz);
}

function levelClearGizmoPrims() {
    foreach (prim in level.gizmoPrims) {
        if (prim == null) continue;
        try {
            local removed = prim.remove();
            // Best-effort: a stale proxy after hot-reload is fine to drop.
            if (removed != null && !removed.ok) {}
        } catch (error) {}
    }
    level.gizmoPrims = [];
}

/**
 * @brief Draw the TransformGizmo as world primitives (missing in the old UI —
 * pick/drag worked but nothing was visible).
 *
 * Primitive factories return Result{ok,value}; keep the owned proxy and set
 * depth to ignore so the gizmo stays readable over terrain.
 */
function levelDrawGizmo() {
    levelClearGizmoPrims();
    if (level.mode == "play") return;
    local object = levelObject(level.selected);
    if (object == null || level.gizmo == null) return;
    local gizmo = level.gizmo;

    function keep(result) {
        if (result == null || !result.ok) return;
        local prim = result.value;
        local depth = prim.setDepthMode("ignore");
        if (depth != null && !depth.ok) {}
        level.gizmoPrims.push(prim);
    }

    for (local i = 0; i < gizmo.getPartCount(); ++i) {
        local ox = gizmo.getPartOriginX(i), oy = gizmo.getPartOriginY(i), oz = gizmo.getPartOriginZ(i);
        local length = gizmo.getPartLength(i);
        local a = levelGizmoWorldPoint(object, ox, oy, oz);
        local b = levelGizmoWorldPoint(object,
            ox + gizmo.getPartDirX(i) * length,
            oy + gizmo.getPartDirY(i) * length,
            oz + gizmo.getPartDirZ(i) * length);
        local kind = gizmo.getPartKind(i);
        local r = gizmo.getPartColorR(i), g = gizmo.getPartColorG(i), bl = gizmo.getPartColorB(i);
        if (kind == "axis") {
            if (gizmo.getMode() == "translate")
                keep(gfx.newPrimitiveArrow3D(a[0], a[1], a[2], b[0], b[1], b[2],
                    length * 0.22, length * 0.08, r, g, bl, 1.0, 3.0));
            else
                keep(gfx.newPrimitiveLine3D(a[0], a[1], a[2], b[0], b[1], b[2], r, g, bl, 1.0, 3.0));
        } else if (kind == "center") {
            local rad = gizmo.getPartRadius(i) * 0.75;
            keep(gfx.newPrimitiveLine3D(a[0] - rad, a[1], a[2], a[0] + rad, a[1], a[2], r, g, bl, 1.0, 2.0));
        } else if (kind == "ring") {
            local normal = [gizmo.getPartDirX(i), gizmo.getPartDirY(i), gizmo.getPartDirZ(i)];
            local u = fabs(normal[1]) < 0.9 ? [-normal[2], 0.0, normal[0]] : [0.0, normal[2], -normal[1]];
            local un = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            if (un < 0.0001) continue;
            for (local k = 0; k < 3; ++k) u[k] /= un;
            local v = [normal[1] * u[2] - normal[2] * u[1],
                       normal[2] * u[0] - normal[0] * u[2],
                       normal[0] * u[1] - normal[1] * u[0]];
            local points = [], radius = gizmo.getPartRadius(i);
            for (local k = 0; k < 48; ++k) {
                local angle = k * 6.28318530718 / 48.0;
                local p = levelGizmoWorldPoint(object,
                    ox + radius * (u[0] * cos(angle) + v[0] * sin(angle)),
                    oy + radius * (u[1] * cos(angle) + v[1] * sin(angle)),
                    oz + radius * (u[2] * cos(angle) + v[2] * sin(angle)));
                points.push(p[0]); points.push(p[1]); points.push(p[2]);
            }
            keep(gfx.newPrimitivePolyline3D(points, true, r, g, bl, 1.0, 3.0));
        }
    }
}

function levelPointer() {
    local down = mouse.isDown(1);
    local pressed = down && !level.mouseDown;
    local released = !down && level.mouseDown;
    level.mouseDown = down;

    local gizmo = level.gizmo;
    local pointer = levelViewportPointer();
    if (pointer == null) {
        if (gizmo.isDragging()) gizmo.endDrag();
        return;
    }
    local object = levelObject(level.selected);
    level.camera.screenToRay(pointer.x, pointer.y, pointer.w, pointer.h);
    local ox = level.camera.getScreenRayOriginX();
    local oy = level.camera.getScreenRayOriginY();
    local oz = level.camera.getScreenRayOriginZ();
    local dx = level.camera.getScreenRayDirX();
    local dy = level.camera.getScreenRayDirY();
    local dz = level.camera.getScreenRayDirZ();

    // The gizmo lives in the parent's local space, so pick and drag there.
    local ax = ox, ay = oy, az = oz;
    local bx = ox + dx, by = oy + dy, bz = oz + dz;
    if (object != null && object.parent != "") {
        local start = scene.worldToLocalAt(levelHost(), object.parent, ax, ay, az);
        local end = scene.worldToLocalAt(levelHost(), object.parent, bx, by, bz);
        ax = start[0]; ay = start[1]; az = start[2];
        bx = end[0]; by = end[1]; bz = end[2];
    }
    local lx = bx - ax, ly = by - ay, lz = bz - az;
    local length = sqrt(lx * lx + ly * ly + lz * lz);
    if (length < 0.00001) return;
    lx /= length; ly /= length; lz /= length;

    if (pressed) {
        local axis = object == null ? "" : gizmo.pick(ax, ay, az, lx, ly, lz);
        if (axis != "") {
            gizmo.beginDrag(axis, ax, ay, az, lx, ly, lz);
        } else if (level.tool == "whitebox") {
            local hit = terrainRayPick(ox, oy, oz, dx, dy, dz, 6000.0);
            if (hit != null) whiteboxPlace(level.piece, hit.x, hit.y + 0.05, hit.z);
            levelMarkPanelsDirty();
        } else if (level.tool == "spawn") {
            local hit = terrainRayPick(ox, oy, oz, dx, dy, dz, 6000.0);
            if (hit != null) spawnPlace(hit.x, hit.y, hit.z, level.yaw);
            levelMarkPanelsDirty();
        } else {
            local picked = scene.pickRayAt(levelHost(), ox, oy, oz, dx, dy, dz);
            if (picked != level.selected) {
                level.selected = picked;
                levelSyncGizmo();
                levelMarkPanelsDirty();
            }
        }
    }
    if (down && gizmo.isDragging()) {
        gizmo.updateDrag(ax, ay, az, lx, ly, lz);
        levelPreviewSelection();
    }
    if (released && gizmo.isDragging()) {
        local position = [gizmo.getPositionX(), gizmo.getPositionY(), gizmo.getPositionZ()];
        local rotation = [gizmo.getRotationX(), gizmo.getRotationY(), gizmo.getRotationZ()];
        local scale = [gizmo.getScaleX(), gizmo.getScaleY(), gizmo.getScaleZ()];
        gizmo.endDrag();
        levelCommand("scene.transform.set.v1", {
            object = level.selected, position = position, rotation = rotation, scale = scale
        });
        whiteboxSyncTransforms();
        spawnSyncTransforms();
        levelMarkPanelsDirty();
    }
}

function levelEditInput() {
    if (levelKeyPressed("F5")) { levelStartPlay(); return; }
    if (levelKeyPressed("Delete")) {
        levelHandleAction("delete");
        return;
    }
    if (levelKeyPressed("f")) levelFrameSelection();
    if (levelKeyPressed("q")) {
        level.distance = levelClamp(level.distance * 1.15, 3.0, 3000.0);
        levelUpdateOrbitCamera();
    }
    if (levelKeyPressed("e")) {
        level.distance = levelClamp(level.distance / 1.15, 3.0, 3000.0);
        levelUpdateOrbitCamera();
    }
    if (levelKeyPressed("1")) levelHandleAction("tool:select");
    if (levelKeyPressed("2")) levelHandleAction("tool:whitebox");
    if (levelKeyPressed("3")) levelHandleAction("tool:spawn");
    if (levelKeyPressed("w") || levelKeyPressed("W")) levelHandleAction("gizmo:translate");
    if (levelKeyPressed("r") || levelKeyPressed("R")) levelHandleAction("gizmo:rotate");
    if (levelKeyPressed("t") || levelKeyPressed("T")) levelHandleAction("gizmo:scale");

    ui.select("viewport");
    local hovered = ui.viewportHovered("level-vp");
    local mouseX = hovered ? ui.viewportMouseX("level-vp") : mouse.getX();
    local mouseY = hovered ? ui.viewportMouseY("level-vp") : mouse.getY();
    if (hovered) {
        local wheel = ui.viewportWheel("level-vp");
        if (wheel != 0.0) {
            if (wheel > 0.0) level.distance = levelClamp(level.distance / 1.12, 3.0, 3000.0);
            else level.distance = levelClamp(level.distance * 1.12, 3.0, 3000.0);
            levelUpdateOrbitCamera();
        }
    }
    local orbit = hovered && mouse.isDown(2);
    if (orbit && level.orbit) {
        level.yaw += (mouseX - level.lastX) * 0.008;
        level.pitch += (mouseY - level.lastY) * 0.006;
        level.pitch = levelClamp(level.pitch, 0.08, 1.45);
        levelUpdateOrbitCamera();
    }
    // Middle-drag pans the focus across the level.
    local pan = hovered && mouse.isDown(3);
    if (pan && level.pan) levelPanCamera(mouseX - level.lastX, mouseY - level.lastY);
    level.orbit = orbit;
    level.pan = pan;
    level.lastX = mouseX;
    level.lastY = mouseY;
}

function levelPlayInput() {
    if (levelKeyPressed("Escape") || levelKeyPressed("F5")) { levelStopPlay(); return; }
    if (levelKeyPressed("F1")) {
        local names = levelCharacterNames();
        for (local i = 0; i < names.len(); ++i)
            if (names[i] == level.play.characterName)
                { levelSwitchPlayCharacter(names[(i + 1) % names.len()]); return; }
        levelSwitchPlayCharacter(names[0]);
    }
    if (levelKeyPressed("F2")) {
        local names = levelMotionSetNames();
        for (local i = 0; i < names.len(); ++i)
            if (names[i] == level.play.motionSetName)
                { levelSwitchPlayMotionSet(names[(i + 1) % names.len()]); return; }
        levelSwitchPlayMotionSet(names[0]);
    }
    local play = level.play;
    if (play == null) return;

    // Mouse look inside the viewport only, so docking chrome never spins the view.
    ui.select("viewport");
    local hovered = ui.viewportHovered("level-vp");
    local mouseX = hovered ? ui.viewportMouseX("level-vp") : play.lastMouseX;
    local mouseY = hovered ? ui.viewportMouseY("level-vp") : play.lastMouseY;
    local deltaX = mouseX - play.lastMouseX, deltaY = mouseY - play.lastMouseY;
    if (hovered && (deltaX < 200.0 && deltaX > -200.0) && (deltaY < 200.0 && deltaY > -200.0))
        levelPlayLook(deltaX, deltaY);
    play.lastMouseX = mouseX;
    play.lastMouseY = mouseY;

    if (levelKeyPressed("q"))
        play.cameraDistance = levelClamp(play.cameraDistance * 1.15, 2.0, 60.0);
    if (levelKeyPressed("e"))
        play.cameraDistance = levelClamp(play.cameraDistance / 1.15, 2.0, 60.0);
}

eve_init = function() {
    levelInit();
    level.model3d = eve.Model3D();
    level.animation = eve.Animation();
    level.physics = eve.Physics();
    level.heightmapTargets = eve.HeightmapTargetModule();

    levelBuildSceneHost();
    local created = eve.SceneEditorModule().createLiveSession("level-designer.scene", levelHost());
    if (!levelChecked(created)) throw level.status;
    level.session = created.value;
    level.gizmo = eve.Editor().newGizmo();
    level.gizmo.setSize(2.4);
    level.gizmo.setSnapTranslate(0.5, 0.5, 0.5);
    level.gizmo.setMode(level.gizmoMode);

    gfx.setBackgroundColor(0.045, 0.052, 0.068, 1.0);
    gfx.setDirectionalLight(-0.42, -1.0, -0.30, 1.25, 1.18, 1.05);
    ui.setTheme("dark");
    levelSetupCamera();
    levelConfigureWorkspace();

    if (terrainBuild(terrainDefaultReference()) == null) throw level.status;
    levelFrameTerrain();
    level.recipes = whiteboxRecipes();
    levelRefreshPanels();

    print("level-designer: terrain " + level.terrain.width + "x" + level.terrain.height +
          " spacing=" + level.terrain.spacingX + " whiteboxRecipes=" + level.recipes.len() + "\n");
    print("level-designer: workspace panels=" + level.workspace.getPanelCount() +
          " (toolbar/outliner/viewport/inspector/palette)\n");
    print("level-designer: 1/2/3 tools | W/R/T gizmo | LMB place/select | RMB orbit | MMB pan | wheel/Q/E zoom | F focus | F5 play\n");
    print("level-designer: in play: mouse look (viewport) | WASD+Shift | Q/E zoom | F1/F2 character/motion | Esc stop\n");
    level.status = "ready: pick Whitebox, choose a piece, click the terrain";
};

eve_update = function(dt) {
    // Overriding eve_update replaces the engine default, which keeps scene node
    // world transforms fresh. Every projection below reads world transforms, so
    // refresh them once per frame before anything else runs.
    scene.updateTransformsAll();
    if (level.mode == "play") {
        levelPlayInput();
        if (level.mode == "play") levelUpdatePlay(dt);
        levelHandleUi();
        if (level.panelsDirty) levelMountPanels();
        levelClearGizmoPrims();
        return;
    }
    levelHandleUi();
    if (level.panelsDirty) levelMountPanels();
    levelEditInput();
    levelPointer();
    whiteboxSyncTransforms();
    spawnSyncTransforms();
    levelDrawGizmo();
};

eve_render = function() {
    gfx.clear();
    ui.select("viewport");
    local canvas = ui.viewportCanvas("level-vp");
    if (canvas != null) gfx.renderScene3DToCanvas(canvas, level.camera);
    ui.beginFrameAndRender();
};

eve_reload <- function() {
    // The level state holds live module handles (session, gizmo, camera, procgen
    // module, camera controller) that a hot-reload snapshot cannot capture, so the
    // designer rebuilds its session from scratch instead of using stale handles.
    levelClearGizmoPrims();
    level = null;
    eve_init();
    level.status = "hot reloaded";
};
