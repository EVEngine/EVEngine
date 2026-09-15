// Project-composed combat action editor. Native code owns the canonical
// timeline, transactions, undo/redo, hit testing and deterministic preview.

const HITBOX_ID = "kaykit-state:hitbox";

persist combatEditor = {
    workspace = null, timeline = null, camera = null, skeleton = null,
    clipEditor = null, boneMouseDown = false,
    clip = null, player = null, knight = null, knightParts = [], skins = [],
    generalLibrary = null, meleeLibrary = null,
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
        schema="eve.action.timeline", schemaVersion=3,
        actionId="combat:light-attack-kaykit", durationNs=ns(duration),
        animationUri="asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop",
        animationSections=[
            { id="kaykit-section:anticipation",
              animationUri="asset://kaykit/Rig_Medium_General.glb#Idle_A",
              startNs=0, endNs=ns(duration * 0.30), blendInNs=0,
              sourceStartNs=0, sourceEndNs=0, blendCurve="ease-in-out" },
            { id="kaykit-section:strike",
              animationUri="asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop",
              startNs=ns(duration * 0.30), endNs=ns(duration * 0.62), blendInNs=ns(duration * 0.06),
              sourceStartNs=0, sourceEndNs=0, blendCurve="ease-in-out" },
            { id="kaykit-section:recovery",
              animationUri="asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_Block",
              startNs=ns(duration * 0.62), endNs=ns(duration), blendInNs=ns(duration * 0.05),
              sourceStartNs=0, sourceEndNs=0, blendCurve="ease-in-out" },
        ],
        splitTimestampsNs=[ns(duration * 0.30), ns(duration * 0.62)],
        montage={ basePlayRate=1.0, looping=false, footIk=false, animationLayer=0,
                  defaultBlendInNs=ns(0.12), defaultBlendOutNs=ns(0.15), blendOutOffsetNs=0,
                  rootMotionHorizontal=true, rootMotionVertical=false, rootMotionRotation=true },
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
    combatEditor.generalLibrary = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_General.glb");
    combatEditor.meleeLibrary = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_CombatMelee.glb");
    local attackIndex = findAnimation(combatEditor.meleeLibrary, "Melee_1H_Attack_Chop");
    if (attackIndex < 0) throw "KayKit attack clip Melee_1H_Attack_Chop was not found";
    combatEditor.skeleton = anim.newSkeletonFromModel(combatEditor.knight);
    combatEditor.clip = anim.newClipFromModel(combatEditor.meleeLibrary, combatEditor.skeleton, attackIndex);
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

function startMontageRuntime() {
    local idle = findAnimation(combatEditor.generalLibrary, "Idle_A");
    local attack = findAnimation(combatEditor.meleeLibrary, "Melee_1H_Attack_Chop");
    local block = findAnimation(combatEditor.meleeLibrary, "Melee_Block");
    if (idle < 0 || attack < 0 || block < 0) throw "KayKit montage source clips are incomplete";
    requireResult(combatEditor.timeline.registerRuntimeClip(
        "asset://kaykit/Rig_Medium_General.glb#Idle_A", combatEditor.generalLibrary,
        combatEditor.skeleton, idle), "Register anticipation clip");
    requireResult(combatEditor.timeline.registerRuntimeClip(
        "asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop", combatEditor.meleeLibrary,
        combatEditor.skeleton, attack), "Register strike clip");
    requireResult(combatEditor.timeline.registerRuntimeClip(
        "asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_Block", combatEditor.meleeLibrary,
        combatEditor.skeleton, block), "Register recovery clip");
    requireResult(combatEditor.timeline.beginRuntime(combatEditor.skeleton), "Start action montage runtime");
}

function buildWorkspace() {
    combatEditor.workspace = editor.newWorkspace("kaykit.combat", "KayKit Combat Action Editor");
    local timelineData = attackTimeline(combatEditor.clip.getDuration());
    combatEditor.timeline = requireResult(
        eve.ActionEditorModule().create("asset.kaykit.light-attack", timelineData),
        "Create action timeline editor");
    combatEditor.clipEditor = requireResult(
        eve.AnimationEditorModule().create("asset.kaykit.light-attack.clip"),
        "Create KayKit clip editor");
    requireResult(combatEditor.clipEditor.loadRuntimeClip(combatEditor.skeleton, combatEditor.clip),
                  "Import KayKit skeleton and clip keys");
    requireResult(combatEditor.timeline.configureWorkspace(combatEditor.workspace), "Compose workspace");
    local dpi = win.getDPIScale();
    combatEditor.workspace.setRegionSize("left", 225.0 / dpi);
    combatEditor.workspace.setRegionSize("right", 340.0 / dpi);
    combatEditor.workspace.setRegionSize("bottom", 330.0 / dpi);
    combatEditor.workspace.layout(win.getWidth().tofloat() / dpi, win.getHeight().tofloat() / dpi);
    startMontageRuntime();
}

function panelAssets() {
    ui.text("KayKit Skeleton", "assets-title");
    ui.text("", "bone-count");
    for (local i = 0; i < combatEditor.clipEditor.getBoneCount(); ++i) {
        local parent = combatEditor.clipEditor.getBoneParent(i);
        ui.listItem((parent == "" ? "" : "  ") + combatEditor.clipEditor.getBoneName(i), "bone-" + i);
    }
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
    ui.text("Joint Inspector", "inspector-title"); ui.text("", "action-uri"); ui.text("", "revision");
    ui.text("", "selected-item");
    ui.separator("inspector-sep-1"); ui.text("Position", "position-label");
    ui.slider("X", 0.0, -2.0, 2.0, "position-x"); ui.slider("Y", 0.0, -2.0, 2.0, "position-y");
    ui.slider("Z", 0.0, -2.0, 2.0, "position-z"); ui.text("Rotation", "rotation-label");
    ui.slider("X", 0.0, -180.0, 180.0, "rotation-x"); ui.slider("Y", 0.0, -180.0, 180.0, "rotation-y");
    ui.slider("Z", 0.0, -180.0, 180.0, "rotation-z"); ui.text("Scale", "scale-label");
    ui.slider("X", 1.0, 0.05, 3.0, "scale-x"); ui.slider("Y", 1.0, 0.05, 3.0, "scale-y");
    ui.slider("Z", 1.0, 0.05, 3.0, "scale-z");
    ui.beginToolbar("joint-tools"); ui.iconButton("plus", "Set Key", "set-key");
    ui.iconButton("minus", "Delete", "delete-key"); ui.iconButton("undo", "", "clip-undo");
    ui.iconButton("redo", "", "clip-redo"); ui.end();
    ui.slider("Key time", 0.0, 0.0, combatEditor.clip.getDuration(), "key-time");
    ui.separator("inspector-sep-2"); ui.text("", "last-event");
    ui.textWrapped("Drag a state body to move it, or drag either edge to resize. Empty-space click seeks.",
                   245.0, "interaction-help");
}

function panelTimeline() {
    ui.beginRow("transport", 8.0); ui.button("Play / Pause", "play-pause");
    ui.button("Restart", "restart"); ui.button("Strike", "jump-strike");
    ui.button("Blend Out", "blend-out"); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    ui.text("Animation keys · click a diamond to select, empty space to scrub", "bone-timeline-label");
    ui.viewport("bone-timeline", combatEditor.workspace.getRegionW("bottom") - 20.0, 118.0);
    ui.text("Montage gameplay tracks", "action-timeline-label");
    ui.viewport("action-timeline", combatEditor.workspace.getRegionW("bottom") - 20.0, 92.0);
    ui.text("", "timeline-status");
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
        ui.setHostMovable(false);
        ui.setHostResizable(false);
        ui.setHostOverlay(false);
    }
    requireResult(combatEditor.timeline.setViewport(combatEditor.workspace.getRegionW("bottom") - 20.0,
                                                    36.0, 145.0), "Configure timeline viewport");
    requireResult(combatEditor.clipEditor.setViewport(combatEditor.workspace.getRegionW("bottom") - 20.0,
                                                      22.0, 145.0), "Configure bone timeline viewport");
    requireResult(combatEditor.timeline.setSnapSeconds(1.0 / 30.0), "Configure frame snapping");
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
            requireResult(combatEditor.timeline.jumpRuntimeSeconds(0.0), "Restart montage runtime");
            combatEditor.timeline.play(); combatEditor.status = "Preview restarted at 0.000 s";
        } else if (id == "jump-strike") {
            requireResult(combatEditor.timeline.jumpRuntimeSection(1), "Jump montage to strike section");
            requireResult(combatEditor.timeline.seekSeconds(combatEditor.timeline.getDuration() * 0.30),
                          "Align editor preview to strike section");
            combatEditor.status = "Runtime jumped to physical strike section";
        } else if (id == "blend-out") {
            requireResult(combatEditor.timeline.beginRuntimeBlendOut(0.20), "Blend out montage runtime");
            combatEditor.status = "Runtime interruption · paired state exits + 0.20 s blend";
        } else if (id == "undo" || id == "redo") {
            applyHistory(id);
        } else if (id == "set-key" || id == "delete-key" || id == "clip-undo" || id == "clip-redo") {
            local result = id == "set-key" ? combatEditor.clipEditor.keySelectedBone() :
                id == "delete-key" ? combatEditor.clipEditor.deleteSelectedKey() :
                id == "clip-undo" ? combatEditor.clipEditor.undo() : combatEditor.clipEditor.redo();
            if (result.ok) requireResult(combatEditor.clipEditor.writeRuntimeClip(combatEditor.clip, combatEditor.skeleton),
                                         "Rebuild edited KayKit clip");
            combatEditor.status = result.ok ? "Animation key edit committed" : result.status.summary;
        } else if (host == "action.assets" && id.find("bone-") == 0) {
            local index = id.slice(5).tointeger();
            requireResult(combatEditor.clipEditor.selectBone(combatEditor.clipEditor.getBoneName(index)), "Select joint");
            combatEditor.status = "Selected joint " + combatEditor.clipEditor.getSelectedBone();
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "action.inspector") {
            ui.select("action.inspector");
            local result = { ok=true, status={summary=""} };
            if (id.find("position-") == 0) result = combatEditor.clipEditor.setSelectedPosition(
                ui.getValue("position-x"), ui.getValue("position-y"), ui.getValue("position-z"));
            else if (id.find("rotation-") == 0) result = combatEditor.clipEditor.setSelectedRotation(
                ui.getValue("rotation-x"), ui.getValue("rotation-y"), ui.getValue("rotation-z"));
            else if (id.find("scale-") == 0) result = combatEditor.clipEditor.setSelectedScale(
                ui.getValue("scale-x"), ui.getValue("scale-y"), ui.getValue("scale-z"));
            else if (id == "key-time") result = combatEditor.clipEditor.moveSelectedKey(ui.getValue("key-time"));
            if (result.ok) requireResult(combatEditor.clipEditor.writeRuntimeClip(combatEditor.clip, combatEditor.skeleton),
                                         "Rebuild edited KayKit clip");
            combatEditor.status = result.ok ? "Joint transform keyed · revision " + combatEditor.clipEditor.getRevision()
                                            : result.status.summary;
        }
        changed = ui.consumeChange();
    }
}

function updateBoneTimelinePointer() {
    ui.select("action.timeline");
    local hovered = ui.viewportHovered("bone-timeline"); local down = hovered && mouse.isDown(1);
    if (down && !combatEditor.boneMouseDown) {
        local result = combatEditor.clipEditor.pointerDown(ui.viewportMouseX("bone-timeline"),
                                                           ui.viewportMouseY("bone-timeline"));
        combatEditor.status = result.ok ? "Animation timeline selection" : result.status.summary;
    }
    combatEditor.boneMouseDown = down;
}

function updateTimelinePointer() {
    ui.select("action.timeline");
    local hovered = ui.viewportHovered("action-timeline");
    local down = hovered && mouse.isDown(1);
    local x = ui.viewportMouseX("action-timeline"); local y = ui.viewportMouseY("action-timeline");
    local wheel = hovered ? ui.viewportWheel("action-timeline") : 0.0;
    if (wheel != 0.0) {
        local width = combatEditor.timeline.getLayoutWidth();
        local anchor = clampf((x - 145.0) / (width - 145.0), 0.0, 1.0);
        requireResult(combatEditor.timeline.zoomTimeline(pow(1.18, wheel), anchor), "Zoom timeline");
    }
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
    combatEditor.player.setTime(combatEditor.clipEditor.getPlayhead());
    local pose = combatEditor.player.getPose();
    pose.computeWorld(combatEditor.skeleton);
    foreach (binding in combatEditor.skins) binding.skin.applyToMesh(gfx, binding.part.getMesh(), pose);
}

function updateLabels() {
    ui.select("action.preview");
    ui.setText("preview-status", format("%s  %.3f / %.3f s",
        combatEditor.timeline.isPlaying() ? "PLAYING" : "PAUSED",
        combatEditor.timeline.getPreviewTime(), combatEditor.timeline.getDuration()));
    ui.select("action.inspector"); ui.setText("action-uri", "Clip: Melee_1H_Attack_Chop");
    ui.setText("revision", "Revision " + combatEditor.clipEditor.getRevision());
    ui.setText("selected-item", "Joint: " + combatEditor.clipEditor.getSelectedBone());
    ui.setText("last-event", combatEditor.lastEvent);
    ui.setValue("position-x", combatEditor.clipEditor.getSelectedPositionX()); ui.setValue("position-y", combatEditor.clipEditor.getSelectedPositionY()); ui.setValue("position-z", combatEditor.clipEditor.getSelectedPositionZ());
    ui.setValue("rotation-x", combatEditor.clipEditor.getSelectedRotationX()); ui.setValue("rotation-y", combatEditor.clipEditor.getSelectedRotationY()); ui.setValue("rotation-z", combatEditor.clipEditor.getSelectedRotationZ());
    ui.setValue("scale-x", combatEditor.clipEditor.getSelectedScaleX()); ui.setValue("scale-y", combatEditor.clipEditor.getSelectedScaleY()); ui.setValue("scale-z", combatEditor.clipEditor.getSelectedScaleZ());
    ui.setValue("key-time", combatEditor.clipEditor.getSelectedKeyTime()); ui.setEnabled("delete-key", combatEditor.clipEditor.hasSelectedKey());
    ui.setEnabled("clip-undo", combatEditor.clipEditor.canUndo()); ui.setEnabled("clip-redo", combatEditor.clipEditor.canRedo());
    ui.select("action.assets"); ui.setText("bone-count", combatEditor.clipEditor.getBoneCount() + " joints");
    ui.setText("asset-selection", "Active: " + combatEditor.clipEditor.getSelectedBone());
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
    for (local tick = 0; tick < combatEditor.timeline.getRulerTickCount(); ++tick) {
        local x = combatEditor.timeline.getRulerTickX(tick);
        local major = combatEditor.timeline.getRulerTickMajor(tick);
        gfx.drawSolidRect(x, 0.0, major ? 1.5 : 1.0, height,
                          major ? 0.30 : 0.18, major ? 0.34 : 0.20, major ? 0.42 : 0.26,
                          major ? 0.65 : 0.35);
    }
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

function drawBoneTimeline() {
    ui.select("action.timeline"); local canvas = ui.viewportCanvas("bone-timeline"); if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear(); local width=combatEditor.clipEditor.getLayoutWidth(); local height=118.0;
    gfx.drawSolidRect(0.0,0.0,width,height,0.04,0.05,0.07,1.0);
    for(local i=0;i<combatEditor.clipEditor.getKeyCount();++i) {
        local selected=combatEditor.clipEditor.getKeySelected(i);
        gfx.drawSolidRect(combatEditor.clipEditor.getKeyX(i)-3.0,combatEditor.clipEditor.getKeyY(i)-3.0,7.0,7.0,
                          selected?1.0:0.35,selected?0.72:0.62,selected?0.18:0.95,1.0);
    }
    gfx.drawSolidRect(combatEditor.clipEditor.getPlayheadX(),0.0,2.0,height,0.98,0.28,0.24,1.0); gfx.setCanvas(null);
}

eve_init = function() {
    buildCharacterPreview(); buildWorkspace(); ui.setTheme("dark"); ui.setScale(0.75);
    ui.setNavKeyboard(true); mountPanels();
    combatEditor.timeline.play();
    combatEditor.status = "KayKit clip loaded · drag timeline items or use the inspector";
    print("combat-action-editor: clip=" + combatEditor.clip.getDuration() + "s tracks=" +
          combatEditor.timeline.getTrackCount() + " items=" + combatEditor.timeline.getItemCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updateTimelinePointer(); updateBoneTimelinePointer(); updatePreviewCamera(); updateKeyboardShortcuts();
    requireResult(combatEditor.clipEditor.seekSeconds(combatEditor.timeline.getPreviewTime()), "Sync animation playhead");
    if (combatEditor.timeline.isPlaying()) {
        local advanced = combatEditor.timeline.update(dt);
        if (!advanced.ok) combatEditor.status = advanced.status.summary;
        if (combatEditor.timeline.isRuntimePlaying()) {
            local runtimeAdvanced = combatEditor.timeline.advanceRuntime(dt);
            if (!runtimeAdvanced.ok) combatEditor.status = runtimeAdvanced.status.summary;
            else {
                if (runtimeAdvanced.value.sectionId != "")
                    combatEditor.status = "Runtime · " + runtimeAdvanced.value.sectionId;
                local count = combatEditor.timeline.getRuntimeEventCount();
                if (count > 0) {
                    local eventIndex = count - 1;
                    combatEditor.lastEvent = combatEditor.timeline.getRuntimeEventKind(eventIndex) + " · " +
                        combatEditor.timeline.getRuntimeEventType(eventIndex) + " @ " +
                        format("%.3f s", combatEditor.timeline.getRuntimeEventSeconds(eventIndex));
                }
            }
        }
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
    drawTimeline(); drawBoneTimeline(); ui.beginFrameAndRender(); combatEditor.frame += 1;
    if (!combatEditor.screenshotSaved && combatEditor.frame > 10 &&
        gfx.saveFramePng("combat-action-editor.png")) {
        combatEditor.screenshotSaved = true;
        print("combat-action-editor: saved combat-action-editor.png\n");
    }
};
