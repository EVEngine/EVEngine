// Project-composed combat action editor. Native code owns the canonical
// timeline, transactions, undo/redo, hit testing and deterministic preview.

const HITBOX_ID = "kaykit-state:hitbox";

persist combatEditor = {
    workspace = null, timeline = null, camera = null, skeleton = null,
    clip = null, player = null, knight = null, knightParts = [], skins = [],
    swordParts = [], knightTexture = null, ground = null, mouseDown = false,
    keyLight = null, fillLight = null,
    status = "Loading KayKit assets...", lastEvent = "No preview event yet",
    selectedAsset = "Melee 1H Attack Chop", selectedItem = "Nothing selected",
    previewOrbiting = false, previewLastX = 0.0, previewLastY = 0.0,
    previewYaw = 0.65, previewPitch = 0.28, previewDistance = 5.3,
    spaceWas = false, undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function ns(seconds) { return (seconds * 1000000000.0).tointeger(); }
function clampf(value, minimum, maximum) { return value < minimum ? minimum : (value > maximum ? maximum : value); }

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function attackTimeline(duration) {
    return {
        schema="eve.action.timeline", schemaVersion=1,
        actionId="combat:light-attack-kaykit", durationNs=ns(duration),
        animationUri="asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop",
        metadata={ character="asset://kaykit/Knight.glb", weapon="asset://kaykit/sword_1handed.gltf",
                   sourceLicense="CC0-1.0" },
        tracks=[
            { id="kaykit-track:gameplay", label="Gameplay", kind="gameplay", muted=false, locked=false,
              notifies=[
                { id="kaykit-notify:damage", type="combat:damage", timeNs=ns(duration * 0.46),
                  payload={ damageType="Damage.Physical.Slash", amount=18 } },
              ],
              states=[
                { id=HITBOX_ID, type="combat:hitbox-window", startNs=ns(duration * 0.30),
                  endNs=ns(duration * 0.62), payload={ hitbox="weapon.main" } },
                { id="kaykit-state:combo", type="input:combo-window", startNs=ns(duration * 0.65),
                  endNs=ns(duration * 0.86), payload={ input="Ability.Combat.Attack.Light" } },
              ] },
            { id="kaykit-track:presentation", label="Presentation", kind="effect", muted=false, locked=false,
              notifies=[
                { id="kaykit-notify:swing-audio", type="presentation:audio", timeNs=ns(duration * 0.32),
                  payload={ uri="asset://audio/sword-whoosh" } },
                { id="kaykit-notify:swing-vfx", type="presentation:vfx", timeNs=ns(duration * 0.34),
                  payload={ uri="asset://vfx/sword-arc" } },
                { id="kaykit-notify:impact-camera", type="presentation:camera", timeNs=ns(duration * 0.46),
                  payload={ cue="combat.light-impact" } },
              ], states=[] },
            { id="kaykit-track:movement", label="Root Motion", kind="movement", muted=false, locked=false,
              notifies=[], states=[
                { id="kaykit-state:root-motion", type="movement:root-motion-window", startNs=0,
                  endNs=ns(duration), payload={ mode="animation" } },
              ] },
        ],
    };
}

function findAnimation(model, name) {
    for (local i = 0; i < model.getAnimationCount(); ++i)
        if (model.getAnimationName(i) == name) return i;
    return -1;
}

function configurePreviewMaterial(part, albedo) {
    // Keep the legacy surface and Material fields synchronized so runtime and
    // editor-preview rendering use the same authored appearance.
    part.setTint(1.0, 1.0, 1.0, 1.0);
    part.setTexture(albedo);
    part.setReceiveLight(true);
    part.setReceiveShadow(true);
    local material = part.getMaterial();
    if (material != null) {
        material.setShadingModel("pbr");
        material.setAlbedoTexture(albedo);
        material.setTint(1.0, 1.0, 1.0, 1.0);
        material.setMetallic(0.0);
        material.setRoughness(0.72);
        material.setReceiveLight(true);
        material.setReceiveShadow(true);
    }
    for (local i = 0; i < part.getPartCount(); ++i) {
        material = part.getPartMaterial(i);
        if (material == null) continue;
        material.setShadingModel("pbr");
        material.setAlbedoTexture(albedo);
        material.setTint(1.0, 1.0, 1.0, 1.0);
        material.setMetallic(0.0);
        material.setRoughness(0.72);
        material.setReceiveLight(true);
        material.setReceiveShadow(true);
    }
}

function buildCharacterPreview() {
    if (anim == null) anim = eve.Animation();
    combatEditor.knight = model3d.newModelDataFromFile("assets/kaykit/Knight.glb");
    combatEditor.knightTexture = gfx.newTextureFromFile("assets/kaykit/knight_texture.png");
    local library = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_CombatMelee.glb");
    local attackIndex = findAnimation(library, "Melee_1H_Attack_Chop");
    if (attackIndex < 0) throw "KayKit attack clip Melee_1H_Attack_Chop was not found";
    combatEditor.skeleton = anim.newSkeletonFromModel(combatEditor.knight);
    combatEditor.clip = anim.newClipFromModel(library, combatEditor.skeleton, attackIndex);
    combatEditor.clip.setLoop(false);
    combatEditor.player = anim.newPlayer(combatEditor.skeleton);
    combatEditor.player.play(combatEditor.clip);
    combatEditor.player.setLoop(false);

    for (local i = 0; i < combatEditor.knight.getMeshCount(); ++i) {
        local part = model3d.createRenderable(gfx, combatEditor.knight, i);
        part.setPosition(0.0, 0.02, 0.0); part.setYaw(-0.25);
        part.setCastShadow(true); configurePreviewMaterial(part, combatEditor.knightTexture);
        combatEditor.knightParts.push(part);
        if (combatEditor.knight.hasBones(i))
            combatEditor.skins.push({ skin=anim.newSkinFromModel(combatEditor.knight, i, combatEditor.skeleton),
                                      part=part });
    }

    // Import the weapon independently too; the same character + weapon + clip
    // combination is covered by test/kaykit_combat_assets.cpp.
    local sword = model3d.newModelDataFromFile("assets/kaykit/sword_1handed.gltf");
    for (local i = 0; i < sword.getMeshCount(); ++i) {
        local part = model3d.createRenderable(gfx, sword, i);
        part.setPosition(1.30, 0.70, 0.0); part.setYaw(-0.25); part.setCastShadow(true);
        configurePreviewMaterial(part, combatEditor.knightTexture);
        combatEditor.swordParts.push(part);
    }
    combatEditor.ground = eve.Renderable3D();
    combatEditor.ground.setMesh(gfx.newMeshCube(1.0));
    combatEditor.ground.setPosition(0.0, -0.08, 0.0); combatEditor.ground.setScale(5.2, 0.12, 4.2);
    combatEditor.ground.setTint(0.10, 0.13, 0.17, 1.0); combatEditor.ground.setRoughness(0.92);
    combatEditor.ground.setReceiveShadow(true);
    combatEditor.camera = eve.Camera3D();
    combatEditor.camera.setEye(3.25, 2.25, 4.25); combatEditor.camera.setTarget(0.1, 0.85, 0.0);
    combatEditor.camera.setUp(0.0, 1.0, 0.0); combatEditor.camera.setFov(40.0);
    combatEditor.camera.setAmbient(0.28, 0.32, 0.38); combatEditor.camera.setActive(true);
    gfx.setBackgroundColor(0.10, 0.13, 0.19, 1.0);
    combatEditor.keyLight = eve.Light3D(); combatEditor.keyLight.setType("dir");
    combatEditor.keyLight.setDirection(-0.45, 1.0, 0.35);
    combatEditor.keyLight.setColor(1.0, 0.92, 0.80, 1.6);
    combatEditor.fillLight = eve.Light3D(); combatEditor.fillLight.setType("point");
    combatEditor.fillLight.setPosition(2.4, 2.8, 3.6); combatEditor.fillLight.setRadius(9.0);
    combatEditor.fillLight.setColor(0.55, 0.70, 1.0, 1.3);
}

function buildWorkspace() {
    combatEditor.workspace = editor.newWorkspace("kaykit.combat", "KayKit Combat Action Editor");
    local timelineData = attackTimeline(combatEditor.clip.getDuration());
    combatEditor.timeline = requireResult(
        editor.newActionTimelineEditor("asset.kaykit.light-attack", timelineData),
        "Create action timeline editor");
    requireResult(combatEditor.timeline.configureWorkspace(combatEditor.workspace), "Compose workspace");
    combatEditor.workspace.setRegionSize("left", 225.0);
    combatEditor.workspace.setRegionSize("right", 285.0);
    combatEditor.workspace.setRegionSize("bottom", 255.0);
    combatEditor.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelAssets() {
    ui.text("KayKit Adventurers", "assets-title");
    ui.textWrapped("Knight.glb + sword_1handed.gltf", 190.0, "assets-models");
    ui.separator("assets-sep-1"); ui.text("Animation", "assets-animation-label");
    ui.listItem("Melee 1H Attack Chop", "clip-attack");
    ui.text("", "asset-selection");
    ui.textWrapped("CC0 assets are bundled so the example and regression test use identical files.",
                   190.0, "assets-license");
    ui.separator("assets-sep-2"); ui.text("Framework coverage", "coverage-title");
    ui.textWrapped("Schema · typed notify · state window · drag · undo · deterministic preview",
                   190.0, "coverage");
}

function panelPreview() {
    ui.text("RMB orbit · wheel zoom · timeline drives pose", "preview-help");
    ui.text("", "preview-status");
    ui.viewport("combat-preview", combatEditor.workspace.getRegionW("center") - 20.0,
                combatEditor.workspace.getRegionH("center") - 92.0);
}

function panelInspector() {
    ui.text("Action Inspector", "inspector-title"); ui.text("", "action-uri"); ui.text("", "revision");
    ui.text("", "selected-item");
    ui.separator("inspector-sep-1"); ui.text("Hitbox window", "hitbox-title");
    ui.slider("Start", combatEditor.timeline.getStateStart(HITBOX_ID), 0.0,
              combatEditor.timeline.getDuration(), "hitbox-start");
    ui.slider("End", combatEditor.timeline.getStateEnd(HITBOX_ID), 0.0,
              combatEditor.timeline.getDuration(), "hitbox-end");
    ui.beginRow("inspector-history", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    ui.separator("inspector-sep-2"); ui.text("", "last-event");
    ui.textWrapped("Drag a state body to move it, or drag either edge to resize. Empty-space click seeks.",
                   245.0, "interaction-help");
}

function panelTimeline() {
    ui.beginRow("transport", 8.0); ui.button("Play / Pause", "play-pause");
    ui.button("Restart", "restart"); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    ui.text("", "timeline-status");
    ui.viewport("action-timeline", combatEditor.workspace.getRegionW("bottom") - 20.0, 112.0);
}

panelBuilders <- { ["action.assets"]=panelAssets, ["action.preview"]=panelPreview,
                   ["action.inspector"]=panelInspector, ["action.timeline"]=panelTimeline };

function mountPanels() {
    for (local i = 0; i < combatEditor.workspace.getPanelCount(); ++i) {
        local id = combatEditor.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(combatEditor.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = combatEditor.workspace.getPanelRegion(i);
        ui.setHostPos(combatEditor.workspace.getRegionX(region), combatEditor.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(combatEditor.workspace.getRegionW(region), combatEditor.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
    requireResult(combatEditor.timeline.setViewport(combatEditor.workspace.getRegionW("bottom") - 20.0,
                                                    36.0, 145.0), "Configure timeline viewport");
}

function applyHistory(command) {
    local result = command == "undo" ? combatEditor.timeline.undo() : combatEditor.timeline.redo();
    combatEditor.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function handleUiEvents() {
    // consumeClick/consumeChange are global queues: route by the host prefix
    // carried by each event instead of draining them once per selected host.
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click); local host = event[0]; local id = event[1];
        if (id == "play-pause") {
            if (combatEditor.timeline.isPlaying()) {
                combatEditor.timeline.pause(); combatEditor.status = "Preview paused";
            } else {
                combatEditor.timeline.play(); combatEditor.status = "Preview playing";
            }
        } else if (id == "restart") {
            requireResult(combatEditor.timeline.seekSeconds(0.0), "Restart preview");
            combatEditor.timeline.play(); combatEditor.status = "Preview restarted at 0.000 s";
        } else if (id == "undo" || id == "redo") {
            applyHistory(id);
        } else if (host == "action.assets" && id == "clip-attack") {
            combatEditor.selectedAsset = "Melee 1H Attack Chop";
            requireResult(combatEditor.timeline.seekSeconds(0.0), "Select attack clip");
            combatEditor.timeline.play(); combatEditor.status = "Selected KayKit melee attack asset";
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "action.inspector" && (id == "hitbox-start" || id == "hitbox-end")) {
            ui.select("action.inspector");
            local start = combatEditor.timeline.getStateStart(HITBOX_ID);
            local finish = combatEditor.timeline.getStateEnd(HITBOX_ID);
            if (id == "hitbox-start") start = ui.getValue("hitbox-start");
            if (id == "hitbox-end") finish = ui.getValue("hitbox-end");
            local result = combatEditor.timeline.resizeState(HITBOX_ID, start, finish);
            combatEditor.status = result.ok ? "Hitbox " + id.slice(7) + " committed · revision " +
                combatEditor.timeline.getRevision() : result.status.summary;
        }
        changed = ui.consumeChange();
    }
}

function updateTimelinePointer() {
    ui.select("action.timeline");
    local hovered = ui.viewportHovered("action-timeline");
    local down = hovered && mouse.isDown(1);
    local x = ui.viewportMouseX("action-timeline"); local y = ui.viewportMouseY("action-timeline");
    if (down && !combatEditor.mouseDown) {
        combatEditor.timeline.pointerDown(x, y, false);
        if (!combatEditor.timeline.isDragging()) {
            requireResult(combatEditor.timeline.seekX(x), "Seek timeline");
            combatEditor.status = "Playhead positioned from timeline";
        } else combatEditor.status = "Dragging selected timeline item";
    } else if (down && combatEditor.timeline.isDragging()) combatEditor.timeline.pointerMove(x);
    else if (!down && combatEditor.mouseDown && combatEditor.timeline.isDragging()) {
        local result = combatEditor.timeline.pointerUp(x);
        combatEditor.status = result.ok ? "Timeline drag committed as one undo step" : result.status.summary;
    }
    combatEditor.mouseDown = down;
}

function updatePreviewCamera() {
    ui.select("action.preview");
    local hovered = ui.viewportHovered("combat-preview");
    local x = ui.viewportMouseX("combat-preview"); local y = ui.viewportMouseY("combat-preview");
    if (hovered)
        combatEditor.previewDistance = clampf(combatEditor.previewDistance -
            ui.viewportWheel("combat-preview") * 0.45, 2.8, 8.0);
    if (hovered && mouse.isDown(2)) {
        if (combatEditor.previewOrbiting) {
            combatEditor.previewYaw -= (x - combatEditor.previewLastX) * 0.009;
            combatEditor.previewPitch = clampf(combatEditor.previewPitch +
                (y - combatEditor.previewLastY) * 0.009, -0.15, 1.1);
        }
        combatEditor.previewLastX = x; combatEditor.previewLastY = y;
        combatEditor.previewOrbiting = true;
    } else combatEditor.previewOrbiting = false;
    local planar = combatEditor.previewDistance * cos(combatEditor.previewPitch);
    combatEditor.camera.setEye(0.1 + planar * sin(combatEditor.previewYaw),
        0.85 + combatEditor.previewDistance * sin(combatEditor.previewPitch),
        planar * cos(combatEditor.previewYaw));
    combatEditor.camera.setTarget(0.1, 0.85, 0.0);
}

function updateKeyboardShortcuts() {
    local space = keyboard.isDown("space") || keyboard.isDown("Space");
    if (space && !combatEditor.spaceWas) {
        if (combatEditor.timeline.isPlaying()) combatEditor.timeline.pause(); else combatEditor.timeline.play();
        combatEditor.status = "Space toggled preview transport";
    }
    combatEditor.spaceWas = space;
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !combatEditor.undoWas) applyHistory("undo");
    if (redo && !combatEditor.redoWas) applyHistory("redo");
    combatEditor.undoWas = undo; combatEditor.redoWas = redo;
}

function updatePose() {
    combatEditor.player.setTime(combatEditor.timeline.getPreviewTime());
    local pose = combatEditor.player.getPose(); pose.computeWorld(combatEditor.skeleton);
    foreach (binding in combatEditor.skins) binding.skin.applyToMesh(gfx, binding.part.getMesh(), pose);
}

function updateLabels() {
    ui.select("action.preview");
    ui.setText("preview-status", format("%s  %.3f / %.3f s",
        combatEditor.timeline.isPlaying() ? "PLAYING" : "PAUSED",
        combatEditor.timeline.getPreviewTime(), combatEditor.timeline.getDuration()));
    ui.select("action.inspector"); ui.setText("action-uri", "combat:light-attack-kaykit");
    ui.setText("revision", "Revision " + combatEditor.timeline.getRevision());
    combatEditor.selectedItem = "Nothing selected";
    for (local i = 0; i < combatEditor.timeline.getItemCount(); ++i)
        if (combatEditor.timeline.getItemSelected(i))
            combatEditor.selectedItem = combatEditor.timeline.getItemType(i);
    ui.setText("selected-item", "Selected: " + combatEditor.selectedItem);
    ui.setText("last-event", combatEditor.lastEvent);
    ui.setValue("hitbox-start", combatEditor.timeline.getStateStart(HITBOX_ID));
    ui.setValue("hitbox-end", combatEditor.timeline.getStateEnd(HITBOX_ID));
    ui.setEnabled("undo", combatEditor.timeline.canUndo());
    ui.setEnabled("redo", combatEditor.timeline.canRedo());
    ui.select("action.assets"); ui.setText("asset-selection", "Active: " + combatEditor.selectedAsset);
    ui.select("action.timeline"); ui.setText("timeline-status", combatEditor.status);
    ui.setEnabled("undo", combatEditor.timeline.canUndo());
    ui.setEnabled("redo", combatEditor.timeline.canRedo());
}

function drawTimeline() {
    ui.select("action.timeline"); local canvas = ui.viewportCanvas("action-timeline");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = combatEditor.timeline.getLayoutWidth(); local height = combatEditor.timeline.getLayoutHeight();
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.035, 0.045, 0.065, 1.0);
    for (local row = 0; row < combatEditor.timeline.getTrackCount(); ++row) {
        local y = row * 36.0; local shade = row % 2 == 0 ? 0.075 : 0.095;
        gfx.drawSolidRect(0.0, y, width, 35.0, shade, shade + 0.012, shade + 0.025, 1.0);
        gfx.drawSolidRect(145.0, y + 34.0, width - 145.0, 1.0, 0.22, 0.24, 0.29, 1.0);
    }
    gfx.drawSolidRect(144.0, 0.0, 1.0, height, 0.34, 0.37, 0.45, 1.0);
    for (local i = 0; i < combatEditor.timeline.getItemCount(); ++i) {
        local selected = combatEditor.timeline.getItemSelected(i);
        local isState = combatEditor.timeline.getItemState(i);
        local r = selected ? 0.98 : (isState ? 0.25 : 0.86);
        local g = selected ? 0.67 : (isState ? 0.58 : 0.38);
        local b = selected ? 0.20 : (isState ? 0.92 : 0.30);
        local x0 = combatEditor.timeline.getItemMinX(i); local x1 = combatEditor.timeline.getItemMaxX(i);
        local y0 = combatEditor.timeline.getItemMinY(i) + 5.0;
        local y1 = combatEditor.timeline.getItemMaxY(i) - 5.0;
        gfx.drawSolidRect(x0, y0, x1 - x0, y1 - y0, r, g, b, 0.95);
        if (isState) {
            gfx.drawSolidRect(x0, y0, 3.0, y1 - y0, 1.0, 0.86, 0.44, 1.0);
            gfx.drawSolidRect(x1 - 3.0, y0, 3.0, y1 - y0, 1.0, 0.86, 0.44, 1.0);
        }
    }
    gfx.drawSolidRect(combatEditor.timeline.getPlayheadX(), 0.0, 2.0, height, 0.98, 0.28, 0.24, 1.0);
    gfx.setCanvas(null);
}

eve_init = function() {
    buildCharacterPreview(); buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels();
    combatEditor.timeline.play();
    combatEditor.status = "KayKit clip loaded · drag timeline items or use the inspector";
    print("combat-action-editor: clip=" + combatEditor.clip.getDuration() + "s tracks=" +
          combatEditor.timeline.getTrackCount() + " items=" + combatEditor.timeline.getItemCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updateTimelinePointer(); updatePreviewCamera(); updateKeyboardShortcuts();
    if (combatEditor.timeline.isPlaying()) {
        local advanced = combatEditor.timeline.update(dt);
        if (!advanced.ok) combatEditor.status = advanced.status.summary;
    }
    if (combatEditor.timeline.getEventCount() > 0) {
        local i = combatEditor.timeline.getEventCount() - 1;
        combatEditor.lastEvent = combatEditor.timeline.getEventKind(i) + " · " +
            combatEditor.timeline.getEventType(i) + " @ " + format("%.3f s", combatEditor.timeline.getEventTime(i));
    }
    updatePose(); updateLabels();
};

eve_render = function() {
    gfx.clear(); ui.select("action.preview"); local preview = ui.viewportCanvas("combat-preview");
    if (preview != null) gfx.renderScene3DToCanvas(preview, combatEditor.camera);
    drawTimeline(); ui.beginFrameAndRender(); combatEditor.frame += 1;
    if (!combatEditor.screenshotSaved && combatEditor.frame > 90 &&
        gfx.saveFramePng("combat-action-editor.png")) {
        combatEditor.screenshotSaved = true;
        print("combat-action-editor: saved combat-action-editor.png\n");
    }
};
