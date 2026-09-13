// Project-composed combat action editor. Native code owns the canonical
// timeline, transactions, undo/redo, hit testing and deterministic preview.

const HITBOX_ID = "kaykit-state:hitbox";

persist combatEditor = {
    workspace = null, timeline = null, camera = null, cameraController = null, skeleton = null,
    actionModule = null, assetCatalog = null, tabs = [], activeTab = 0, closeArmed = -1, tabUiSignature = "",
    assetPickerOpen = false, assetSearch = "",
    inspectorMode = "joint", selectedActionId = "", selectedActionState = false,
    insertPanelOpen = false, newSectionOpen = false, nextSectionId = 0,
    insertTrack = 0, insertNotifyType = 0, insertStateType = 2,
    selectedTrack = 0, selectedSplit = 0, newTrackOpen = false, newTrackKind = 0, nextTrackId = 0,
    clipEditor = null, boneMouseDown = false,
    clip = null, player = null, knight = null, knightParts = [], skins = [],
    generalLibrary = null, meleeLibrary = null,
    swordParts = [], knightTexture = null, ground = null, mouseDown = false,
    keyLight = null, fillLight = null,
    status = "Loading KayKit assets...", lastEvent = "No preview event yet",
    selectedAsset = "Melee 1H Attack Chop", selectedItem = "Nothing selected",
    previewOrbiting = false, previewLastX = 0.0, previewLastY = 0.0,
    previewYaw = 0.65, previewPitch = 0.28, previewDistance = 5.3,
    spaceWas = false, undoWas = false, redoWas = false, saveWas = false,
    frame = 0, screenshotSaved = false,
};

// Persisted editor state survives script revisions. Add newly introduced UI
// fields explicitly so old workspaces migrate without discarding open tabs.
local editorUiDefaults = {
    insertPanelOpen=false, newSectionOpen=false, nextSectionId=0,
    insertTrack=0, insertNotifyType=0, insertStateType=2,
    selectedTrack=0, selectedSplit=0, newTrackOpen=false, newTrackKind=0, nextTrackId=0,
};
foreach (key, value in editorUiDefaults)
    if (!(key in combatEditor)) combatEditor[key] <- value;

function ns(seconds) { return (seconds * 1000000000.0).tointeger(); }
function clampf(value, minimum, maximum) { return value < minimum ? minimum : (value > maximum ? maximum : value); }

function trackChoices() {
    local choices = "";
    for (local i = 0; i < combatEditor.timeline.getTrackCount(); ++i) {
        if (i > 0) choices += "\n";
        choices += combatEditor.timeline.getTrackLabel(i) + " [" + combatEditor.timeline.getTrackKind(i) + "]";
    }
    return choices;
}

function sectionSplitChoices() {
    local choices = "";
    for (local i = 0; i < combatEditor.timeline.getSectionSplitCount(); ++i) {
        if (i > 0) choices += "\n";
        choices += "Split " + (i + 1);
    }
    return choices == "" ? "No physical splits" : choices;
}

function sectionSplitIndexAt(time) {
    local nearest = 0; local distance = combatEditor.timeline.getDuration();
    for (local i = 0; i < combatEditor.timeline.getSectionSplitCount(); ++i) {
        local delta = fabs(combatEditor.timeline.getSectionSplitTime(i) - time);
        if (delta < distance) { nearest = i; distance = delta; }
    }
    return nearest;
}

function trackKindChoices() { return "Animation\nGameplay\nEffect\nAudio\nCamera\nMovement\nTag\nCustom"; }
function trackKindAt(index) {
    local kinds = ["animation", "gameplay", "effect", "audio", "camera", "movement", "tag", "custom"];
    return kinds[clampf(index, 0, kinds.len() - 1).tointeger()];
}

function insertTypeChoices(state) {
    local choices = "";
    for (local i = 0; i < combatEditor.timeline.getInsertableTypeCount(state); ++i) {
        if (i > 0) choices += "\n";
        choices += combatEditor.timeline.getInsertableTypeLabel(state, i);
    }
    return choices;
}

function defaultPayloadForType(type) {
    local payloads = {
        ["combat:damage"]="{\"damageType\":\"Damage.Physical\",\"amount\":1}",
        ["gameplay:event"]="{\"tag\":\"Gameplay.Event\"}",
        ["presentation:vfx"]="{\"uri\":\"asset://vfx/effect\",\"lifetimeSeconds\":1.0}",
        ["presentation:audio"]="{\"uri\":\"asset://audio/clip\"}",
        ["presentation:camera"]="{\"cue\":\"camera:impact\"}",
        ["collision:ignore-window"]="{\"channel\":\"default\"}",
        ["combat:hitbox-window"]="{\"hitbox\":\"weapon.main\"}",
        ["combat:invulnerability-window"]="{}",
        ["gameplay:prefab-spawn"]="{\"uri\":\"asset://prefabs/object\"}",
        ["input:combo-window"]="{\"input\":\"Ability.Attack\"}",
        ["movement:root-motion-window"]="{\"mode\":\"animation\"}",
        ["presentation:audio-state"]="{\"uri\":\"asset://audio/clip\"}",
        ["presentation:vfx-state"]="{\"uri\":\"asset://vfx/effect\"}",
        ["presentation:parameter-curve"]="{\"target\":\"audio:master-volume\",\"keys\":[]}",
    };
    return type in payloads ? payloads[type] : "{}";
}

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function attackTimeline(duration) {
    return {
        schema="eve.action.timeline", schemaVersion=4,
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
                  payload={ damageType="Damage.Physical.Slash", amount=18 }, enabled=true },
              ],
              states=[
                { id=HITBOX_ID, type="combat:hitbox-window", startNs=ns(duration * 0.30),
                  endNs=ns(duration * 0.62), payload={ hitbox="weapon.main" }, enabled=true },
                { id="kaykit-state:combo", type="input:combo-window", startNs=ns(duration * 0.65),
                  endNs=ns(duration * 0.86), payload={ input="Ability.Combat.Attack.Light" }, enabled=true },
              ] },
            { id="kaykit-track:presentation", label="Presentation", kind="effect", muted=false, locked=false,
              notifies=[
                { id="kaykit-notify:swing-audio", type="presentation:audio", timeNs=ns(duration * 0.32),
                  payload={ uri="asset://audio/sword-whoosh" }, enabled=true },
                { id="kaykit-notify:swing-vfx", type="presentation:vfx", timeNs=ns(duration * 0.34),
                  payload={ uri="asset://vfx/sword-arc", lifetimeSeconds=0.45 }, enabled=true },
                { id="kaykit-notify:impact-camera", type="presentation:camera", timeNs=ns(duration * 0.46),
                  payload={ cue="combat:light-impact", positionAmplitude=0.08,
                            rotationAmplitude=1.8, fovAmplitude=2.0,
                            durationSeconds=0.24, seed=47 }, enabled=true },
              ], states=[
                { id="kaykit-state:volume-curve", type="presentation:parameter-curve",
                  startNs=ns(duration * 0.20), endNs=ns(duration * 0.75),
                  payload={ target="audio:master-volume", operation="multiply", keys=[
                      { time=0.0, value=0.35, inTangent=0.0, outTangent=2.2, interpolation="cubic" },
                      { time=0.35, value=1.0, inTangent=0.0, outTangent=0.0, interpolation="cubic" },
                      { time=1.0, value=0.55, inTangent=-0.8, outTangent=0.0, interpolation="linear" },
                  ] }, enabled=true },
              ] },
            { id="kaykit-track:movement", label="Root Motion", kind="movement", muted=false, locked=false,
              notifies=[], states=[
                { id="kaykit-state:root-motion", type="movement:root-motion-window", startNs=0,
                  endNs=ns(duration), payload={ mode="animation" }, enabled=true },
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
    combatEditor.cameraController = eve.CameraController();
    combatEditor.cameraController.setCamera(combatEditor.camera);
    combatEditor.cameraController.setMode("orbit");
    combatEditor.cameraController.setTarget(0.1, 0.85, 0.0);
    combatEditor.cameraController.setFov(40.0);
    combatEditor.cameraController.setSmooth(30.0);
    combatEditor.cameraController.setActionCuesEnabled(true);
    gfx.setBackgroundColor(0.10, 0.13, 0.19, 1.0);
    combatEditor.keyLight = eve.Light3D(); combatEditor.keyLight.setType("dir");
    combatEditor.keyLight.setDirection(-0.45, 1.0, 0.35);
    combatEditor.keyLight.setColor(1.0, 0.92, 0.80, 1.6);
    combatEditor.fillLight = eve.Light3D(); combatEditor.fillLight.setType("point");
    combatEditor.fillLight.setPosition(2.4, 2.8, 3.6); combatEditor.fillLight.setRadius(9.0);
    combatEditor.fillLight.setColor(0.55, 0.70, 1.0, 1.3);
}

function startMontageRuntime(timeline) {
    local idle = findAnimation(combatEditor.generalLibrary, "Idle_A");
    local attack = findAnimation(combatEditor.meleeLibrary, "Melee_1H_Attack_Chop");
    local block = findAnimation(combatEditor.meleeLibrary, "Melee_Block");
    if (idle < 0 || attack < 0 || block < 0) throw "KayKit montage source clips are incomplete";
    requireResult(timeline.registerRuntimeClip(
        "asset://kaykit/Rig_Medium_General.glb#Idle_A", combatEditor.generalLibrary,
        combatEditor.skeleton, idle), "Register anticipation clip");
    requireResult(timeline.registerRuntimeClip(
        "asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop", combatEditor.meleeLibrary,
        combatEditor.skeleton, attack), "Register strike clip");
    requireResult(timeline.registerRuntimeClip(
        "asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_Block", combatEditor.meleeLibrary,
        combatEditor.skeleton, block), "Register recovery clip");
    requireResult(timeline.beginRuntime(combatEditor.skeleton), "Start action montage runtime");
}

function openActionDocument(assetGuid, title, resourceUri, timelineData) {
    local timeline = requireResult(combatEditor.actionModule.openDocument(
        ".", assetGuid, title, resourceUri, timelineData), "Open " + title);
    startMontageRuntime(timeline);
    combatEditor.tabs.push({ title=title, assetGuid=assetGuid, resourceUri=resourceUri, editor=timeline });
    return timeline;
}

function findOpenAsset(assetGuid) {
    for (local i = 0; i < combatEditor.tabs.len(); ++i)
        if (combatEditor.tabs[i].assetGuid == assetGuid) return i;
    return -1;
}

function openBundledDocument(assetGuid) {
    local existing = findOpenAsset(assetGuid);
    if (existing >= 0) { activateTab(existing); return; }
    local timelineData = attackTimeline(combatEditor.clip.getDuration());
    if (assetGuid == "asset.kaykit.light-attack") {
        openActionDocument(assetGuid, "Light Attack", "content://Actions/KayKitLightAttack.action", timelineData);
    } else {
        timelineData.actionId = "combat:follow-up-kaykit";
        timelineData.metadata.variant <- "follow-up";
        openActionDocument(assetGuid, "Follow-up", "content://Actions/KayKitFollowUp.action", timelineData);
    }
    activateTab(combatEditor.tabs.len() - 1);
}

function saveActiveDocument() {
    local tab = combatEditor.tabs[combatEditor.activeTab];
    local saved = tab.editor.saveDocument();
    if (!saved.ok) { combatEditor.status = "Save failed · " + saved.status.summary; return; }
    local indexed = combatEditor.assetCatalog.registerDocument(tab.assetGuid, tab.resourceUri);
    if (!indexed.ok) {
        combatEditor.status = "Saved, but AssetDB registration failed · " + indexed.status.summary;
        return;
    }
    combatEditor.assetCatalog.refresh(combatEditor.assetSearch);
    combatEditor.status = "Saved and indexed · " + tab.resourceUri;
    combatEditor.closeArmed = -1; combatEditor.tabUiSignature = "";
}

function mountAssetPicker() {
    ui.beginBuild(); ui.beginWindow("Open Montage Asset", "root");
    ui.searchField("Search Content paths", combatEditor.assetSearch, "asset-search");
    ui.text(combatEditor.assetCatalog.getAssetCount() + " validated Montage assets", "asset-count");
    ui.beginScrollList("asset-results", 315.0, 25.0);
    for (local i = 0; i < combatEditor.assetCatalog.getAssetCount(); ++i)
        ui.listItem(combatEditor.assetCatalog.getAssetTitle(i) + "  ·  " +
                    combatEditor.assetCatalog.getAssetUri(i), "pick-asset-" + i);
    ui.end();
    ui.beginToolbar("asset-picker-actions"); ui.iconButton("close", "Cancel", "close-asset-picker"); ui.end();
    ui.end(); ui.mountBuildAs("action.asset-picker"); ui.select("action.asset-picker");
    ui.setHostOverlay(true); ui.setHostModal(true); ui.setHostMovable(false); ui.setHostResizable(false);
    ui.setHostPos(330.0, 150.0, 0.0, 0.0); ui.setHostSize(620.0, 420.0); ui.setHostVisible(true);
}

function showAssetPicker() {
    local refreshed = combatEditor.assetCatalog.refresh(combatEditor.assetSearch);
    combatEditor.status = refreshed.ok ? "AssetDB query · " + refreshed.value + " Montage assets"
                                       : "AssetDB query failed · " + refreshed.status.summary;
    combatEditor.assetPickerOpen = true;
    mountAssetPicker();
}

function hideAssetPicker() {
    combatEditor.assetPickerOpen = false;
    ui.select("action.asset-picker"); ui.setHostVisible(false); ui.setHostModal(false);
}

function openCatalogAsset(index) {
    if (index < 0 || index >= combatEditor.assetCatalog.getAssetCount()) return;
    local guid = combatEditor.assetCatalog.getAssetGuid(index);
    local existing = findOpenAsset(guid);
    if (existing >= 0) { hideAssetPicker(); activateTab(existing); return; }
    local title = combatEditor.assetCatalog.getAssetTitle(index);
    local uri = combatEditor.assetCatalog.getAssetUri(index);
    openActionDocument(guid, title, uri, attackTimeline(combatEditor.clip.getDuration()));
    hideAssetPicker(); activateTab(combatEditor.tabs.len() - 1);
}

function activateTab(index) {
    if (index < 0 || index >= combatEditor.tabs.len()) return;
    combatEditor.activeTab = index;
    combatEditor.timeline = combatEditor.tabs[index].editor;
    combatEditor.closeArmed = -1;
    requireResult(combatEditor.timeline.setViewport(combatEditor.workspace.getRegionW("bottom") - 20.0,
                                                    36.0, 145.0), "Configure active timeline viewport");
    requireResult(combatEditor.timeline.setSnapSeconds(1.0 / 30.0), "Configure active frame snapping");
    combatEditor.status = "Active document · " + combatEditor.tabs[index].title;
    combatEditor.tabUiSignature = "";
}

function closeTab(index) {
    if (index < 0 || index >= combatEditor.tabs.len()) return;
    if (combatEditor.tabs.len() == 1) {
        combatEditor.status = "Keep at least one montage document open";
        return;
    }
    local tab = combatEditor.tabs[index];
    if (tab.editor.isDirty() && combatEditor.closeArmed != index) {
        combatEditor.closeArmed = index;
        combatEditor.status = "Unsaved changes in " + tab.title + " · click close again to discard";
        combatEditor.tabUiSignature = "";
        return;
    }
    combatEditor.tabs.remove(index);
    local next = combatEditor.activeTab;
    if (index < next) next -= 1;
    if (next >= combatEditor.tabs.len()) next = combatEditor.tabs.len() - 1;
    activateTab(next);
}

function buildWorkspace() {
    combatEditor.workspace = editor.newWorkspace("kaykit.combat", "KayKit Combat Action Editor");
    combatEditor.actionModule = eve.ActionEditorModule();
    combatEditor.assetCatalog = requireResult(combatEditor.actionModule.createAssetCatalog("."),
                                              "Create Action AssetDB browser");
    requireResult(combatEditor.assetCatalog.refresh(""), "Scan Montage assets");
    combatEditor.tabs = [];
    local timelineData = attackTimeline(combatEditor.clip.getDuration());
    openActionDocument("asset.kaykit.light-attack", "Light Attack",
                       "content://Actions/KayKitLightAttack.action", timelineData);
    local alternate = attackTimeline(combatEditor.clip.getDuration());
    alternate.actionId = "combat:follow-up-kaykit";
    alternate.metadata.variant <- "follow-up";
    openActionDocument("asset.kaykit.follow-up", "Follow-up",
                       "content://Actions/KayKitFollowUp.action", alternate);
    combatEditor.activeTab = 0;
    combatEditor.timeline = combatEditor.tabs[0].editor;
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
    ui.text("Inspector", "inspector-title");
    ui.beginToolbar("inspector-tabs");
    ui.iconButton("bone", "Joint", "inspect-joint"); ui.setItemSelected(combatEditor.inspectorMode == "joint");
    ui.iconButton("layers", "Action Block", "inspect-action"); ui.setItemSelected(combatEditor.inspectorMode == "action");
    ui.iconButton("list", "Track", "inspect-track"); ui.setItemSelected(combatEditor.inspectorMode == "track");
    ui.iconButton("settings", "Montage", "inspect-montage"); ui.setItemSelected(combatEditor.inspectorMode == "montage");
    ui.end();
    ui.text("", "action-uri"); ui.text("", "revision");
    if (combatEditor.inspectorMode == "montage") {
        ui.text("Montage Settings", "montage-title");
        ui.slider("Play Rate", 1.0, 0.05, 4.0, "montage-rate");
        ui.checkbox("Looping", false, "montage-looping");
        ui.checkbox("Foot IK", false, "montage-foot-ik");
        ui.slider("Animation Layer", 0.0, 0.0, 8.0, "montage-layer");
        ui.slider("Default Blend In", 0.0, 0.0, combatEditor.timeline.getDuration(), "montage-blend-in");
        ui.slider("Default Blend Out", 0.0, 0.0, combatEditor.timeline.getDuration(), "montage-blend-out");
        ui.slider("Blend Out Offset", 0.0, -combatEditor.timeline.getDuration(),
                  combatEditor.timeline.getDuration(), "montage-blend-offset");
        ui.text("Root Motion", "montage-root-title");
        ui.checkbox("Horizontal", true, "montage-root-horizontal");
        ui.checkbox("Vertical", true, "montage-root-vertical");
        ui.checkbox("Rotation", true, "montage-root-rotation");
        ui.text("Physical Sections", "physical-sections-title");
        ui.combo("Split", sectionSplitChoices(), combatEditor.selectedSplit, "section-split-selector");
        ui.slider("Split Time", 0.5, 0.0, combatEditor.timeline.getDuration(), "section-split-time");
        ui.beginToolbar("section-split-tools");
        ui.iconButton("plus", "Add at Playhead", "add-section-split");
        ui.iconButton("trash", "Delete Split", "delete-section-split");
        ui.end();
        ui.beginToolbar("montage-tools"); ui.iconButton("undo", "", "montage-undo");
        ui.iconButton("redo", "", "montage-redo"); ui.end();
        ui.textWrapped("Settings update the authoritative asset and the running preview without rebuilding clips.",
                       245.0, "montage-help");
        return;
    }
    if (combatEditor.inspectorMode == "track") {
        if (combatEditor.newTrackOpen) {
            ui.text("New Track", "new-track-title");
            ui.inputText("Name", "New Track", "new-track-label");
            ui.combo("Kind", trackKindChoices(), combatEditor.newTrackKind, "new-track-kind");
            ui.beginToolbar("new-track-actions"); ui.iconButton("plus", "Create", "add-track");
            ui.iconButton("close", "Cancel", "cancel-new-track"); ui.end();
            ui.textWrapped("Track creation is validated and committed as one undoable transaction.",
                           245.0, "new-track-help");
            return;
        }
        ui.text("Track Inspector", "track-title");
        ui.combo("Track", trackChoices(), combatEditor.selectedTrack, "track-selector");
        ui.text("", "track-id"); ui.text("", "track-kind");
        ui.inputText("Name", "", "track-label");
        ui.checkbox("Muted", false, "track-muted"); ui.checkbox("Locked", false, "track-locked");
        ui.beginToolbar("track-tools"); ui.iconButton("plus", "New Track", "new-track");
        ui.iconButton("copy", "Copy Track", "copy-track");
        ui.iconButton("clipboard", "Paste Track", "paste-track");
        ui.iconButton("trash", "Delete Track", "delete-track");
        ui.iconButton("undo", "", "track-undo"); ui.iconButton("redo", "", "track-redo"); ui.end();
        ui.textWrapped("Muted tracks do not emit events. Locked tracks reject item edits and paste operations.",
                       245.0, "track-help");
        return;
    }
    if (combatEditor.inspectorMode == "action") {
        if (combatEditor.newSectionOpen) {
            local sectionStart = combatEditor.timeline.getPreviewTime();
            local sectionEnd = clampf(sectionStart + 0.25, sectionStart, combatEditor.timeline.getDuration());
            ui.text("New Animation Section", "new-section-title");
            ui.inputText("Animation URI", combatEditor.timeline.getAnimationUri(), "new-section-uri");
            ui.slider("Start", sectionStart, 0.0, combatEditor.timeline.getDuration(), "new-section-start");
            ui.slider("End", sectionEnd, 0.0, combatEditor.timeline.getDuration(), "new-section-end");
            ui.slider("Blend In", 0.10, 0.0, combatEditor.timeline.getDuration(), "new-section-blend");
            ui.beginToolbar("new-section-actions"); ui.iconButton("plus", "Create", "add-section");
            ui.iconButton("close", "Cancel", "cancel-new-section"); ui.end();
            ui.textWrapped("Sections use exact timeline time and are rejected if they overlap or exceed the montage.",
                           245.0, "new-section-help");
            return;
        }
        if (combatEditor.insertPanelOpen) {
            ui.text("New Block at Playhead", "insert-title");
            ui.combo("Track", trackChoices(), combatEditor.insertTrack, "insert-track");
            ui.combo("Instant", insertTypeChoices(false), combatEditor.insertNotifyType, "insert-notify-type");
            ui.inputText("Payload", defaultPayloadForType(
                combatEditor.timeline.getInsertableType(false, combatEditor.insertNotifyType)), "insert-notify-payload");
            ui.iconButton("plus", "Add Notify", "insert-notify");
            ui.separator("insert-separator");
            ui.combo("State", insertTypeChoices(true), combatEditor.insertStateType, "insert-state-type");
            ui.slider("Duration", 0.25, 1.0 / 30.0, combatEditor.timeline.getDuration(), "insert-state-duration");
            ui.inputText("Payload", defaultPayloadForType(
                combatEditor.timeline.getInsertableType(true, combatEditor.insertStateType)), "insert-state-payload");
            ui.beginToolbar("insert-actions"); ui.iconButton("plus", "Add State", "insert-state");
            ui.iconButton("close", "Cancel", "cancel-insert"); ui.end();
            ui.textWrapped("Types come from the native registry. Payload contracts are validated before commit.",
                           245.0, "insert-help");
            return;
        }
        ui.text("Action Block", "action-block-title"); ui.text("No Action Block selected", "action-block-id");
        ui.text("", "action-block-kind");
        ui.text("Selection: 0 blocks", "action-selection-summary");
        ui.beginToolbar("selection-tools");
        ui.iconButton("align-left", "Align Starts", "align-selection-start");
        ui.iconButton("align-right", "Align Ends", "align-selection-end");
        ui.end();
        ui.checkbox("Enabled", true, "action-enabled");
        ui.text("Type", "action-type-label"); ui.inputText("##Type", "", "action-type");
        ui.text("Resource", "typed-resource-title");
        ui.inputText("URI", "", "payload-uri");
        ui.text("Audio", "typed-audio-title");
        ui.inputText("Random URIs (; separated)", "", "audio-random-uris");
        ui.slider("Volume", 1.0, 0.0, 2.0, "audio-volume");
        ui.slider("Pitch", 1.0, 0.05, 4.0, "audio-pitch");
        ui.slider("Pitch Variation", 0.0, 0.0, 0.5, "audio-random-pitch");
        ui.slider("Spatial Blend", 1.0, 0.0, 1.0, "audio-spatial-blend");
        ui.slider("Min Distance", 1.0, 0.01, 50.0, "audio-min-distance");
        ui.slider("Max Distance", 35.0, 0.1, 200.0, "audio-max-distance");
        ui.checkbox("Looping", false, "audio-looping");
        ui.checkbox("Fade Out on Exit", true, "audio-fade-out");
        ui.slider("Fade Duration", 0.1, 0.01, 2.0, "audio-fade-duration");
        ui.text("VFX", "typed-vfx-title");
        ui.combo("Stop", "Stop Emitting\nClear Immediately", 0, "vfx-stop");
        ui.checkbox("Sync Rate", true, "vfx-sync-rate");
        ui.slider("Clip Start", 0.0, 0.0, combatEditor.timeline.getDuration(), "vfx-clip-start");
        ui.slider("Clip End", 0.5, 0.0, combatEditor.timeline.getDuration(), "vfx-clip-end");
        ui.iconButton("move", "Fit Block to Clip", "vfx-fit-block");
        ui.iconButton("sliders", "Fit Clip to Block", "vfx-fit-clip");
        ui.slider("Lifetime", 1.0, 0.01, 10.0, "vfx-lifetime");
        ui.text("Prefab", "typed-prefab-title");
        ui.combo("Lifecycle", "Recycle on Exit\nCustom Duration\nIndependent", 0, "prefab-lifecycle");
        ui.slider("Custom Duration", 1.0, 0.01, 10.0, "prefab-duration");
        ui.text("Spatial Attachment", "typed-spatial-title");
        ui.combo("Attachment", "Follow Target\nFollow Position\nWorld at Start", 0, "spatial-attachment");
        ui.combo("Anchor", "Source\nTarget", 0, "spatial-target");
        ui.slider("Target Index", 0.0, 0.0, 8.0, "spatial-target-index");
        ui.inputText("Bone", "", "spatial-bone");
        ui.text("Position Offset", "spatial-position-title");
        ui.slider("X", 0.0, -5.0, 5.0, "spatial-position-x");
        ui.slider("Y", 0.0, -5.0, 5.0, "spatial-position-y");
        ui.slider("Z", 0.0, -5.0, 5.0, "spatial-position-z");
        ui.text("Rotation Offset", "spatial-rotation-title");
        ui.slider("X", 0.0, -180.0, 180.0, "spatial-rotation-x");
        ui.slider("Y", 0.0, -180.0, 180.0, "spatial-rotation-y");
        ui.slider("Z", 0.0, -180.0, 180.0, "spatial-rotation-z");
        ui.text("Spawn Scale", "spatial-scale-title");
        ui.slider("X", 1.0, 0.05, 4.0, "spatial-scale-x");
        ui.slider("Y", 1.0, 0.05, 4.0, "spatial-scale-y");
        ui.slider("Z", 1.0, 0.05, 4.0, "spatial-scale-z");
        ui.setItemSize(300.0, 0.0);
        ui.text("Advanced Payload JSON", "action-payload-label"); ui.inputText("##Payload", "{}", "action-payload");
        ui.setItemSize(300.0, 0.0);
        ui.slider("Start", 0.0, 0.0, combatEditor.timeline.getDuration(), "action-start");
        ui.slider("End", 0.0, 0.0, combatEditor.timeline.getDuration(), "action-end");
        ui.text("Animation Section", "section-fields-title");
        ui.inputText("Animation URI", "", "section-uri");
        ui.slider("Blend In", 0.0, 0.0, combatEditor.timeline.getDuration(), "section-blend");
        ui.slider("Source Start", 0.0, 0.0, combatEditor.timeline.getDuration(), "section-source-start");
        ui.slider("Source End", 0.0, 0.0, combatEditor.timeline.getDuration(), "section-source-end");
        ui.combo("Blend Curve", "Linear\nEase In Out", 1, "section-curve");
        ui.beginToolbar("action-tools"); ui.iconButton("plus", "New Block", "new-action");
        ui.iconButton("layers", "New Section", "new-section");
        ui.iconButton("copy", "Copy", "copy-action");
        ui.iconButton("clipboard", "Paste at playhead", "paste-action");
        ui.iconButton("trash", "Delete Block", "delete-action");
        ui.iconButton("undo", "", "action-undo"); ui.iconButton("redo", "", "action-redo"); ui.end();
        ui.textWrapped("Timing edits are validated and committed as undoable timeline transactions.",
                       245.0, "action-help");
        return;
    }
    ui.text("Joint Transform", "joint-title");
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

function selectedActionIndex() {
    for (local i = 0; i < combatEditor.timeline.getItemCount(); ++i)
        if (combatEditor.timeline.getItemSelected(i)) return i;
    return -1;
}

function selectedActionCount() {
    local count = 0;
    for (local i = 0; i < combatEditor.timeline.getItemCount(); ++i)
        if (combatEditor.timeline.getItemSelected(i)) count += 1;
    return count;
}

function selectedActionRange() {
    local start = combatEditor.timeline.getDuration(); local finish = 0.0; local found = false;
    for (local i = 0; i < combatEditor.timeline.getItemCount(); ++i) {
        if (!combatEditor.timeline.getItemSelected(i)) continue;
        local id = combatEditor.timeline.getItemId(i);
        local itemStart = combatEditor.timeline.getItemStart(id);
        local itemEnd = combatEditor.timeline.getItemEnd(id);
        if (itemStart < start) start = itemStart;
        if (itemEnd > finish) finish = itemEnd;
        found = true;
    }
    return found ? [start, finish] : [0.0, 0.0];
}

function animationSectionIndex(itemId) {
    for (local i = 0; i < combatEditor.timeline.getAnimationSectionCount(); ++i)
        if (combatEditor.timeline.getAnimationSectionId(i) == itemId) return i;
    return -1;
}

function blendCurveIndex(name) { return name == "linear" ? 0 : 1; }
function blendCurveAt(index) { return index == 0 ? "linear" : "ease-in-out"; }
function isAudioType(type) { return type == "presentation:audio" || type == "presentation:audio-state"; }
function isVfxType(type) { return type == "presentation:vfx" || type == "presentation:vfx-state"; }
function isPrefabType(type) { return type == "gameplay:prefab-spawn"; }
function isSpatialType(type) { return isAudioType(type) || isVfxType(type) || isPrefabType(type); }
function attachmentIndex(value) {
    return value == "follow_position_only" ? 1 : (value == "world_transform_at_start" ? 2 : 0);
}
function attachmentAt(index) {
    return index == 1 ? "follow_position_only" : (index == 2 ? "world_transform_at_start" : "follow_target");
}
function lifecycleIndex(value) {
    return value == "custom_duration" ? 1 : (value == "independent" ? 2 : 0);
}
function lifecycleAt(index) {
    return index == 1 ? "custom_duration" : (index == 2 ? "independent" : "recycle_on_block_exit");
}
function stopBehaviorIndex(value) { return value == "clear_immediately" ? 1 : 0; }
function stopBehaviorAt(index) { return index == 1 ? "clear_immediately" : "stop_emitting"; }

function setTypedPayloadVisibility(type, single) {
    local audio = single && isAudioType(type);
    local vfx = single && isVfxType(type);
    local prefab = single && isPrefabType(type);
    local spatial = single && isSpatialType(type);
    foreach (field in ["typed-resource-title", "payload-uri"]) ui.setVisible(field, spatial);
    foreach (field in ["typed-audio-title", "audio-random-uris", "audio-volume", "audio-pitch",
                       "audio-random-pitch", "audio-spatial-blend", "audio-min-distance", "audio-max-distance",
                       "audio-fade-out", "audio-fade-duration"])
        ui.setVisible(field, audio);
    ui.setVisible("audio-looping", audio && type == "presentation:audio-state");
    foreach (field in ["typed-vfx-title", "vfx-stop", "vfx-sync-rate", "vfx-clip-start", "vfx-clip-end",
                       "vfx-fit-block", "vfx-fit-clip"])
        ui.setVisible(field, vfx);
    ui.setVisible("vfx-fit-block", vfx && type == "presentation:vfx-state");
    ui.setVisible("vfx-fit-clip", vfx && type == "presentation:vfx-state");
    ui.setVisible("vfx-lifetime", vfx && type == "presentation:vfx");
    foreach (field in ["typed-prefab-title", "prefab-lifecycle", "prefab-duration"])
        ui.setVisible(field, prefab);
    foreach (field in ["typed-spatial-title", "spatial-attachment", "spatial-target", "spatial-target-index",
                       "spatial-bone", "spatial-position-title", "spatial-position-x", "spatial-position-y",
                       "spatial-position-z", "spatial-rotation-title", "spatial-rotation-x", "spatial-rotation-y",
                       "spatial-rotation-z", "spatial-scale-title", "spatial-scale-x", "spatial-scale-y",
                       "spatial-scale-z"]) ui.setVisible(field, spatial);
}

function commitPayloadVector(field, prefix) {
    return combatEditor.timeline.setItemPayloadVector3(combatEditor.selectedActionId, field,
        ui.getValue(prefix + "-x"), ui.getValue(prefix + "-y"), ui.getValue(prefix + "-z"));
}

function commitMontageSettings() {
    return combatEditor.timeline.setMontageSettings(
        ui.getValue("montage-rate"), ui.getChecked("montage-looping"), ui.getChecked("montage-foot-ik"),
        ui.getValue("montage-layer").tointeger(), ui.getValue("montage-blend-in"),
        ui.getValue("montage-blend-out"), ui.getValue("montage-blend-offset"),
        ui.getChecked("montage-root-horizontal"), ui.getChecked("montage-root-vertical"),
        ui.getChecked("montage-root-rotation"));
}

function panelTimeline() {
    ui.beginToolbar("documents");
    ui.iconButton("folder-open", "Open Asset", "open-document");
    ui.iconButton("save", "Save", "save-document");
    for (local i = 0; i < combatEditor.tabs.len(); ++i) {
        local tab = combatEditor.tabs[i];
        ui.iconButton("file", tab.title + (tab.editor.isDirty() ? " *" : ""), "tab-" + i);
        ui.setItemSelected(i == combatEditor.activeTab);
        ui.iconButton("close", combatEditor.closeArmed == i ? "Discard" : "", "close-tab-" + i);
    }
    ui.end();
    ui.beginToolbar("transport");
    ui.iconButton("home", "", "first-frame"); ui.iconButton("chevron-left", "", "previous-frame");
    ui.iconButton("play", "", "play-pause"); ui.iconButton("stop", "", "stop");
    ui.iconButton("chevron-right", "", "next-frame"); ui.iconButton("refresh", "", "last-frame");
    ui.iconButton("layers", "Strike", "jump-strike"); ui.iconButton("close", "Blend Out", "blend-out");
    ui.iconButton("undo", "", "undo"); ui.iconButton("redo", "", "redo"); ui.end();
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

function mountTimelinePanel() {
    ui.beginBuild(); ui.beginWindow("Timeline", "root"); panelTimeline(); ui.end();
    ui.mountBuildAs("action.timeline"); ui.select("action.timeline");
    ui.setHostPos(combatEditor.workspace.getRegionX("bottom"), combatEditor.workspace.getRegionY("bottom"), 0.0, 0.0);
    ui.setHostSize(combatEditor.workspace.getRegionW("bottom"), combatEditor.workspace.getRegionH("bottom"));
    ui.setHostMovable(false); ui.setHostResizable(false); ui.setHostOverlay(false);
}

function mountInspectorPanel() {
    ui.beginBuild(); ui.beginWindow("Inspector", "root"); panelInspector(); ui.end();
    ui.mountBuildAs("action.inspector"); ui.select("action.inspector");
    ui.setHostPos(combatEditor.workspace.getRegionX("right"), combatEditor.workspace.getRegionY("right"), 0.0, 0.0);
    ui.setHostSize(combatEditor.workspace.getRegionW("right"), combatEditor.workspace.getRegionH("right"));
    ui.setHostMovable(false); ui.setHostResizable(false); ui.setHostOverlay(false);
}

function tabUiSignature() {
    local value = combatEditor.activeTab + ":" + combatEditor.closeArmed;
    foreach (tab in combatEditor.tabs) value += ":" + tab.title + ":" + (tab.editor.isDirty() ? "1" : "0");
    return value;
}

function refreshDocumentToolbar() {
    local signature = tabUiSignature();
    if (signature == combatEditor.tabUiSignature) return;
    combatEditor.tabUiSignature = signature;
    mountTimelinePanel();
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
        if (id == "save-document") {
            saveActiveDocument();
        } else if (id == "open-document") {
            showAssetPicker();
        } else if (id == "close-asset-picker") {
            hideAssetPicker();
        } else if (id.find("pick-asset-") == 0) {
            openCatalogAsset(id.slice(11).tointeger());
        } else if (id.find("close-tab-") == 0) {
            closeTab(id.slice(10).tointeger());
        } else if (id.find("tab-") == 0) {
            activateTab(id.slice(4).tointeger());
        } else if (id == "play-pause") {
            if (combatEditor.timeline.isPlaying()) {
                combatEditor.timeline.pause(); combatEditor.status = "Preview paused";
            } else {
                combatEditor.timeline.play(); combatEditor.status = "Preview playing";
            }
        } else if (id == "first-frame") {
            requireResult(combatEditor.timeline.stop(), "Jump to first frame");
            requireResult(combatEditor.timeline.jumpRuntimeSeconds(0.0), "Restart montage runtime");
            combatEditor.status = "Preview stopped at first frame";
        } else if (id == "previous-frame" || id == "next-frame") {
            requireResult(combatEditor.timeline.stepFrames(id == "previous-frame" ? -1 : 1, 30.0),
                          "Step preview frame");
            combatEditor.status = id == "previous-frame" ? "Previous frame" : "Next frame";
        } else if (id == "stop") {
            requireResult(combatEditor.timeline.stop(), "Stop preview");
            combatEditor.status = "Preview stopped at 0.000 s";
        } else if (id == "last-frame") {
            requireResult(combatEditor.timeline.jumpToEnd(), "Jump to last frame");
            combatEditor.status = "Preview moved to final frame";
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
        } else if (id == "inspect-joint" || id == "inspect-action" || id == "inspect-track" ||
                   id == "inspect-montage") {
            combatEditor.inspectorMode = id == "inspect-action" ? "action" :
                (id == "inspect-track" ? "track" : (id == "inspect-montage" ? "montage" : "joint"));
            combatEditor.insertPanelOpen = false;
            combatEditor.newSectionOpen = false;
            combatEditor.newTrackOpen = false;
            mountInspectorPanel();
        } else if (id == "new-action" || id == "cancel-insert") {
            combatEditor.insertPanelOpen = id == "new-action";
            combatEditor.newSectionOpen = false;
            mountInspectorPanel();
        } else if (id == "new-section" || id == "cancel-new-section") {
            combatEditor.newSectionOpen = id == "new-section";
            combatEditor.insertPanelOpen = false;
            mountInspectorPanel();
        } else if (id == "add-section") {
            ui.select("action.inspector"); combatEditor.nextSectionId += 1;
            local result = combatEditor.timeline.addAnimationSection(
                "editor-section:section-" + combatEditor.nextSectionId, ui.getValueText("new-section-uri"),
                ui.getValue("new-section-start"), ui.getValue("new-section-end"), ui.getValue("new-section-blend"));
            combatEditor.status = result.ok ? "Animation Section created" : result.status.summary;
            if (result.ok) { combatEditor.newSectionOpen = false; mountInspectorPanel(); }
        } else if (id == "new-track" || id == "cancel-new-track") {
            combatEditor.newTrackOpen = id == "new-track";
            mountInspectorPanel();
        } else if (id == "add-track") {
            ui.select("action.inspector");
            combatEditor.nextTrackId += 1;
            local trackId = "editor-track:custom-" + combatEditor.nextTrackId;
            local result = combatEditor.timeline.addTrack(trackId, ui.getValueText("new-track-label"),
                                                          trackKindAt(combatEditor.newTrackKind));
            combatEditor.status = result.ok ? "Track created" : result.status.summary;
            if (result.ok) {
                combatEditor.selectedTrack = combatEditor.timeline.getTrackCount() - 1;
                combatEditor.newTrackOpen = false; mountInspectorPanel();
            }
        } else if (id == "copy-track" || id == "paste-track" || id == "delete-track") {
            local result = id == "paste-track" ? combatEditor.timeline.pasteTrack() :
                (id == "copy-track" ? combatEditor.timeline.copyTrack(
                    combatEditor.timeline.getTrackId(combatEditor.selectedTrack)) :
                    combatEditor.timeline.removeTrack(combatEditor.timeline.getTrackId(combatEditor.selectedTrack)));
            combatEditor.status = result.ok ? (id == "copy-track" ? "Track copied" :
                (id == "paste-track" ? "Track pasted" : "Track deleted")) : result.status.summary;
            if (result.ok && id == "paste-track") combatEditor.selectedTrack = combatEditor.timeline.getTrackCount() - 1;
            if (combatEditor.selectedTrack >= combatEditor.timeline.getTrackCount())
                combatEditor.selectedTrack = combatEditor.timeline.getTrackCount() - 1;
        } else if (id == "track-undo" || id == "track-redo") {
            applyHistory(id == "track-undo" ? "undo" : "redo");
        } else if (id == "montage-undo" || id == "montage-redo") {
            applyHistory(id == "montage-undo" ? "undo" : "redo");
        } else if (id == "add-section-split") {
            local splitTime = combatEditor.timeline.getPreviewTime();
            local result = combatEditor.timeline.addSectionSplit(splitTime);
            if (result.ok) {
                combatEditor.selectedSplit = sectionSplitIndexAt(splitTime);
                mountInspectorPanel();
            }
            combatEditor.status = result.ok ? "Physical section split added" : result.status.summary;
        } else if (id == "delete-section-split") {
            local result = combatEditor.timeline.removeSectionSplit(combatEditor.selectedSplit);
            if (result.ok) {
                combatEditor.selectedSplit = combatEditor.timeline.getSectionSplitCount() > 0 ?
                    clampf(combatEditor.selectedSplit, 0, combatEditor.timeline.getSectionSplitCount() - 1).tointeger() : 0;
                mountInspectorPanel();
            }
            combatEditor.status = result.ok ? "Physical section split deleted" : result.status.summary;
        } else if (id == "align-selection-start" || id == "align-selection-end") {
            local result = combatEditor.timeline.handleTimelineShortcut(
                id == "align-selection-start" ? "Shift+[" : "Shift+]");
            combatEditor.status = result.ok ? (id == "align-selection-start" ?
                "Selected blocks aligned to earliest start" : "Selected blocks aligned to latest end") :
                result.status.summary;
        } else if (id == "delete-action" && combatEditor.selectedActionId != "") {
            local result = combatEditor.timeline.handleTimelineShortcut("Delete");
            combatEditor.status = result.ok ? "Selected Action Blocks deleted" : result.status.summary;
            if (result.ok) combatEditor.selectedActionId = "";
        } else if (id == "copy-action") {
            local result = combatEditor.timeline.handleTimelineShortcut("Ctrl+C");
            combatEditor.status = result.ok ? "Action Block copied" : result.status.summary;
        } else if (id == "paste-action") {
            local result = combatEditor.timeline.handleTimelineShortcut("Ctrl+V");
            combatEditor.status = result.ok ? "Action Block pasted at playhead" : result.status.summary;
        } else if (id == "insert-notify" || id == "insert-state") {
            ui.select("action.inspector");
            local state = id == "insert-state";
            local typeIndex = state ? combatEditor.insertStateType : combatEditor.insertNotifyType;
            local trackId = combatEditor.timeline.getTrackId(combatEditor.insertTrack);
            local type = combatEditor.timeline.getInsertableType(state, typeIndex);
            local payload = ui.getValueText(state ? "insert-state-payload" : "insert-notify-payload");
            local result = state ? combatEditor.timeline.addStateAtCursor(
                trackId, type, ui.getValue("insert-state-duration"), payload) :
                combatEditor.timeline.addNotifyAtCursor(trackId, type, payload);
            combatEditor.status = result.ok ? (state ? "Notify State inserted" : "Notify inserted")
                                            : result.status.summary;
        } else if (id == "action-undo" || id == "action-redo") {
            applyHistory(id == "action-undo" ? "undo" : "redo");
        } else if ((id == "vfx-fit-block" || id == "vfx-fit-clip") &&
                   combatEditor.selectedActionId != "") {
            local result = id == "vfx-fit-block" ?
                combatEditor.timeline.fitBlockToClip(combatEditor.selectedActionId) :
                combatEditor.timeline.fitClipToBlock(combatEditor.selectedActionId);
            combatEditor.status = result.ok ? (id == "vfx-fit-block" ?
                "VFX block fitted to clip at 1.0x" : "VFX clip fitted to block") : result.status.summary;
            if (result.ok) mountInspectorPanel();
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
        if (host == "action.asset-picker" && id == "asset-search") {
            ui.select("action.asset-picker"); combatEditor.assetSearch = ui.getValueText("asset-search");
            local refreshed = combatEditor.assetCatalog.refresh(combatEditor.assetSearch);
            combatEditor.status = refreshed.ok ? "AssetDB query · " + refreshed.value + " Montage assets"
                                               : "AssetDB query failed · " + refreshed.status.summary;
            mountAssetPicker();
        } else if (host == "action.inspector") {
            ui.select("action.inspector");
            local result = { ok=true, status={summary=""} };
            if (id == "section-split-selector") {
                combatEditor.selectedSplit = ui.getValue(id).tointeger();
            } else if (id == "section-split-time" && combatEditor.timeline.getSectionSplitCount() > 0) {
                local splitTime = ui.getValue(id);
                result = combatEditor.timeline.setSectionSplit(combatEditor.selectedSplit, splitTime);
                if (result.ok) combatEditor.selectedSplit = sectionSplitIndexAt(splitTime);
                combatEditor.status = result.ok ? "Physical section split moved" : result.status.summary;
            } else if (id.find("montage-") == 0 && id != "montage-tools") {
                result = commitMontageSettings();
                combatEditor.status = result.ok ? "Montage settings committed" : result.status.summary;
            } else if (id == "insert-track") combatEditor.insertTrack = ui.getValue("insert-track").tointeger();
            else if (id == "track-selector")
                combatEditor.selectedTrack = ui.getValue("track-selector").tointeger();
            else if (id == "new-track-kind")
                combatEditor.newTrackKind = ui.getValue("new-track-kind").tointeger();
            else if (id == "track-label" && combatEditor.timeline.getTrackCount() > 0) {
                result = combatEditor.timeline.renameTrack(combatEditor.timeline.getTrackId(combatEditor.selectedTrack),
                                                           ui.getValueText("track-label"));
                combatEditor.status = result.ok ? "Track renamed" : result.status.summary;
            } else if ((id == "track-muted" || id == "track-locked") && combatEditor.timeline.getTrackCount() > 0) {
                local trackId = combatEditor.timeline.getTrackId(combatEditor.selectedTrack);
                result = id == "track-muted" ? combatEditor.timeline.setTrackMuted(trackId, ui.getChecked(id)) :
                                               combatEditor.timeline.setTrackLocked(trackId, ui.getChecked(id));
                combatEditor.status = result.ok ? "Track flags committed" : result.status.summary;
            }
            else if (id == "insert-notify-type") {
                combatEditor.insertNotifyType = ui.getValue("insert-notify-type").tointeger();
                ui.setValueText("insert-notify-payload", defaultPayloadForType(
                    combatEditor.timeline.getInsertableType(false, combatEditor.insertNotifyType)));
            } else if (id == "insert-state-type") {
                combatEditor.insertStateType = ui.getValue("insert-state-type").tointeger();
                ui.setValueText("insert-state-payload", defaultPayloadForType(
                    combatEditor.timeline.getInsertableType(true, combatEditor.insertStateType)));
            } else if (id == "payload-uri" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadText(
                    combatEditor.selectedActionId, "uri", ui.getValueText(id));
            } else if (id == "audio-random-uris" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadTextList(
                    combatEditor.selectedActionId, "randomUris", ui.getValueText(id));
            } else if ((id == "audio-volume" || id == "audio-pitch" || id == "audio-random-pitch" ||
                        id == "audio-spatial-blend" || id == "audio-min-distance" || id == "audio-max-distance" ||
                        id == "audio-fade-duration") &&
                       combatEditor.selectedActionId != "") {
                local field = id == "audio-volume" ? "volume" : (id == "audio-pitch" ? "pitch" :
                    (id == "audio-random-pitch" ? "randomPitchOffset" :
                    (id == "audio-spatial-blend" ? "spatialBlend" :
                    (id == "audio-min-distance" ? "minDistance" :
                    (id == "audio-max-distance" ? "maxDistance" : "fadeOutDuration")))));
                result = combatEditor.timeline.setItemPayloadNumber(
                    combatEditor.selectedActionId, field, ui.getValue(id));
            } else if ((id == "audio-looping" || id == "audio-fade-out") &&
                       combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadBool(
                    combatEditor.selectedActionId, id == "audio-looping" ? "looping" : "fadeOutOnExit",
                    ui.getChecked(id));
            } else if (id == "vfx-stop" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadText(combatEditor.selectedActionId,
                    "stopBehavior", stopBehaviorAt(ui.getValue(id).tointeger()));
            } else if (id == "vfx-sync-rate" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadBool(
                    combatEditor.selectedActionId, "playbackRateSynced", ui.getChecked(id));
            } else if ((id == "vfx-clip-start" || id == "vfx-clip-end" || id == "vfx-lifetime") &&
                       combatEditor.selectedActionId != "") {
                local field = id == "vfx-clip-start" ? "clipStartTime" :
                    (id == "vfx-clip-end" ? "clipEndTime" : "lifetimeSeconds");
                result = combatEditor.timeline.setItemPayloadNumber(
                    combatEditor.selectedActionId, field, ui.getValue(id));
            } else if (id == "prefab-lifecycle" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.patchItemPayload(combatEditor.selectedActionId,
                    format("{\"lifecycle\":\"%s\",\"customDurationSeconds\":%.6f}",
                           lifecycleAt(ui.getValue(id).tointeger()), ui.getValue("prefab-duration")));
            } else if (id == "prefab-duration" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadNumber(
                    combatEditor.selectedActionId, "customDurationSeconds", ui.getValue(id));
            } else if (id == "spatial-attachment" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadText(combatEditor.selectedActionId,
                    "attachment", attachmentAt(ui.getValue(id).tointeger()));
            } else if (id == "spatial-target" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadText(combatEditor.selectedActionId,
                    "spatialTarget", ui.getValue(id).tointeger() == 1 ? "target" : "source");
            } else if (id == "spatial-target-index" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadInteger(
                    combatEditor.selectedActionId, "targetIndex", ui.getValue(id).tointeger());
            } else if (id == "spatial-bone" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemPayloadText(
                    combatEditor.selectedActionId, "bone", ui.getValueText(id));
            } else if (id.find("spatial-position-") == 0) {
                result = commitPayloadVector("positionOffset", "spatial-position");
            } else if (id.find("spatial-rotation-") == 0) {
                result = commitPayloadVector("rotationOffsetDegrees", "spatial-rotation");
            } else if (id.find("spatial-scale-") == 0) {
                result = commitPayloadVector("scale", "spatial-scale");
            } else if ((id == "action-start" || id == "action-end") && combatEditor.selectedActionId != "") {
                local start = ui.getValue("action-start");
                local finish = combatEditor.selectedActionState ? ui.getValue("action-end") : start;
                result = combatEditor.timeline.setItemTiming(combatEditor.selectedActionId, start, finish);
                combatEditor.status = result.ok ? "Action Block timing committed" : result.status.summary;
            } else if (id == "action-enabled" && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.setItemEnabled(combatEditor.selectedActionId,
                                                               ui.getChecked("action-enabled"));
                combatEditor.status = result.ok ? "Action Block enabled state committed" : result.status.summary;
            } else if ((id == "action-type" || id == "action-payload") && combatEditor.selectedActionId != "") {
                result = combatEditor.timeline.editItemDetails(combatEditor.selectedActionId,
                    ui.getValueText("action-type"), ui.getValueText("action-payload"));
                combatEditor.status = result.ok ? "Action Block details committed" : result.status.summary;
            } else if ((id == "section-uri" || id == "section-blend") &&
                       animationSectionIndex(combatEditor.selectedActionId) >= 0) {
                result = combatEditor.timeline.editAnimationSection(combatEditor.selectedActionId,
                    ui.getValue("action-start"), ui.getValue("action-end"), ui.getValue("section-blend"),
                    ui.getValueText("section-uri"));
                combatEditor.status = result.ok ? "Animation Section committed" : result.status.summary;
            } else if ((id == "section-source-start" || id == "section-source-end" || id == "section-curve") &&
                       animationSectionIndex(combatEditor.selectedActionId) >= 0) {
                result = combatEditor.timeline.editAnimationSectionSource(combatEditor.selectedActionId,
                    ui.getValue("section-source-start"), ui.getValue("section-source-end"),
                    blendCurveAt(ui.getValue("section-curve").tointeger()));
                combatEditor.status = result.ok ? "Section source trim committed" : result.status.summary;
            } else if (id.find("position-") == 0) result = combatEditor.clipEditor.setSelectedPosition(
                ui.getValue("position-x"), ui.getValue("position-y"), ui.getValue("position-z"));
            else if (id.find("rotation-") == 0) result = combatEditor.clipEditor.setSelectedRotation(
                ui.getValue("rotation-x"), ui.getValue("rotation-y"), ui.getValue("rotation-z"));
            else if (id.find("scale-") == 0) result = combatEditor.clipEditor.setSelectedScale(
                ui.getValue("scale-x"), ui.getValue("scale-y"), ui.getValue("scale-z"));
            else if (id == "key-time") result = combatEditor.clipEditor.moveSelectedKey(ui.getValue("key-time"));
            local jointChange = id.find("position-") == 0 || id.find("rotation-") == 0 ||
                                id.find("scale-") == 0 || id == "key-time";
            if (jointChange && result.ok)
                requireResult(combatEditor.clipEditor.writeRuntimeClip(combatEditor.clip, combatEditor.skeleton),
                              "Rebuild edited KayKit clip");
            if (jointChange)
                combatEditor.status = result.ok ? "Joint transform keyed · revision " + combatEditor.clipEditor.getRevision()
                                                : result.status.summary;
            else if (!result.ok) combatEditor.status = result.status.summary;
            else if (id.find("payload-") == 0 || id.find("audio-") == 0 || id.find("vfx-") == 0 ||
                     id.find("prefab-") == 0 || id.find("spatial-") == 0)
                combatEditor.status = "Typed Action Block properties committed";
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
        local additive = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl") ||
                         keyboard.isDown("lshift") || keyboard.isDown("rshift") || keyboard.isDown("shift");
        combatEditor.timeline.pointerDown(x, y, additive);
        local selectedIndex = selectedActionIndex();
        if (selectedIndex >= 0) {
            combatEditor.selectedActionId = combatEditor.timeline.getItemId(selectedIndex);
            combatEditor.selectedActionState = combatEditor.timeline.getItemState(selectedIndex);
            if (combatEditor.inspectorMode != "action") {
                combatEditor.inspectorMode = "action";
                mountInspectorPanel();
            }
        }
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

function updatePreviewCamera(dt) {
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
    combatEditor.cameraController.setRadius(combatEditor.previewDistance);
    combatEditor.cameraController.setAzimuth(combatEditor.previewYaw * 57.2957795);
    combatEditor.cameraController.setElevation(combatEditor.previewPitch * 57.2957795);
    combatEditor.cameraController.update(dt);
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
    local save = control && (keyboard.isDown("s") || keyboard.isDown("S"));
    if (undo && !combatEditor.undoWas) applyHistory("undo");
    if (redo && !combatEditor.redoWas) applyHistory("redo");
    if (save && !combatEditor.saveWas) {
        saveActiveDocument();
    }
    combatEditor.undoWas = undo; combatEditor.redoWas = redo; combatEditor.saveWas = save;
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
    if (combatEditor.inspectorMode == "action") {
        local selected = selectedActionIndex();
        local selectionCount = selectedActionCount();
        local selectionRange = selectedActionRange();
        ui.setText("action-selection-summary", selectionCount == 0 ? "Selection: 0 blocks" :
            format("Selection: %d blocks · %.3f–%.3f s", selectionCount, selectionRange[0], selectionRange[1]));
        ui.setEnabled("align-selection-start", selectionCount > 1);
        ui.setEnabled("align-selection-end", selectionCount > 1);
        if (selected >= 0) {
            combatEditor.selectedActionId = combatEditor.timeline.getItemId(selected);
            combatEditor.selectedActionState = combatEditor.timeline.getItemState(selected);
            local selectedType = combatEditor.timeline.getItemType(selected);
            local sectionIndex = animationSectionIndex(combatEditor.selectedActionId);
            local isSection = sectionIndex >= 0;
            local single = selectionCount == 1;
            setTypedPayloadVisibility(selectedType, single && !isSection);
            ui.setText("action-block-id", combatEditor.selectedActionId);
            ui.setText("action-block-kind", isSection ? "Animation section" :
                       (combatEditor.selectedActionState ? "State window" : "Instant notify"));
            ui.setValueText("action-type", selectedType);
            ui.setValueText("action-payload", combatEditor.timeline.getItemPayloadJson(combatEditor.selectedActionId));
            ui.setChecked("action-enabled", combatEditor.timeline.getItemEnabled(combatEditor.selectedActionId));
            ui.setValue("action-start", combatEditor.timeline.getItemStart(combatEditor.selectedActionId));
            ui.setValue("action-end", combatEditor.timeline.getItemEnd(combatEditor.selectedActionId));
            ui.setEnabled("action-start", single);
            ui.setEnabled("action-end", single && (combatEditor.selectedActionState || isSection));
            ui.setEnabled("action-enabled", single && !isSection);
            ui.setEnabled("action-type", single && !isSection);
            ui.setEnabled("action-payload", single && !isSection);
            ui.setVisible("action-type-label", !isSection); ui.setVisible("action-type", !isSection);
            ui.setVisible("action-payload-label", !isSection); ui.setVisible("action-payload", !isSection);
            ui.setVisible("action-enabled", !isSection);
            foreach (field in ["section-fields-title", "section-uri", "section-blend", "section-source-start",
                               "section-source-end", "section-curve"]) ui.setVisible(field, isSection);
            if (isSection) {
                ui.setValueText("section-uri", combatEditor.timeline.getAnimationSectionUri(sectionIndex));
                ui.setValue("section-blend", combatEditor.timeline.getAnimationSectionBlendIn(sectionIndex));
                ui.setValue("section-source-start", combatEditor.timeline.getAnimationSectionSourceStart(sectionIndex));
                ui.setValue("section-source-end", combatEditor.timeline.getAnimationSectionSourceEnd(sectionIndex));
                ui.setValue("section-curve", blendCurveIndex(
                    combatEditor.timeline.getAnimationSectionBlendCurve(sectionIndex)).tofloat());
            } else if (single && isSpatialType(selectedType)) {
                local itemId = combatEditor.selectedActionId;
                ui.setValueText("payload-uri", combatEditor.timeline.getItemPayloadText(itemId, "uri", ""));
                ui.setValue("spatial-attachment", attachmentIndex(
                    combatEditor.timeline.getItemPayloadText(itemId, "attachment", "follow_target")).tofloat());
                ui.setValue("spatial-target", combatEditor.timeline.getItemPayloadText(
                    itemId, "spatialTarget", "source") == "target" ? 1.0 : 0.0);
                ui.setValue("spatial-target-index", combatEditor.timeline.getItemPayloadNumber(
                    itemId, "targetIndex", 0.0));
                ui.setValueText("spatial-bone", combatEditor.timeline.getItemPayloadText(itemId, "bone", ""));
                foreach (axis, component in { x=0, y=1, z=2 }) {
                    ui.setValue("spatial-position-" + axis, combatEditor.timeline.getItemPayloadVector(
                        itemId, "positionOffset", component, 0.0));
                    ui.setValue("spatial-rotation-" + axis, combatEditor.timeline.getItemPayloadVector(
                        itemId, "rotationOffsetDegrees", component, 0.0));
                    ui.setValue("spatial-scale-" + axis, combatEditor.timeline.getItemPayloadVector(
                        itemId, "scale", component, 1.0));
                }
                if (isAudioType(selectedType)) {
                    ui.setValueText("audio-random-uris", combatEditor.timeline.getItemPayloadTextList(
                        itemId, "randomUris"));
                    ui.setValue("audio-volume", combatEditor.timeline.getItemPayloadNumber(itemId, "volume", 1.0));
                    ui.setValue("audio-pitch", combatEditor.timeline.getItemPayloadNumber(itemId, "pitch", 1.0));
                    ui.setValue("audio-random-pitch", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "randomPitchOffset", 0.0));
                    ui.setValue("audio-spatial-blend", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "spatialBlend", 1.0));
                    ui.setValue("audio-min-distance", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "minDistance", 1.0));
                    ui.setValue("audio-max-distance", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "maxDistance", 35.0));
                    ui.setChecked("audio-looping", combatEditor.timeline.getItemPayloadBool(itemId, "looping", false));
                    ui.setChecked("audio-fade-out", combatEditor.timeline.getItemPayloadBool(
                        itemId, "fadeOutOnExit", true));
                    ui.setValue("audio-fade-duration", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "fadeOutDuration", 0.1));
                } else if (isVfxType(selectedType)) {
                    ui.setValue("vfx-stop", stopBehaviorIndex(combatEditor.timeline.getItemPayloadText(
                        itemId, "stopBehavior", "stop_emitting")).tofloat());
                    ui.setChecked("vfx-sync-rate", combatEditor.timeline.getItemPayloadBool(
                        itemId, "playbackRateSynced", true));
                    ui.setValue("vfx-clip-start", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "clipStartTime", 0.0));
                    ui.setValue("vfx-clip-end", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "clipEndTime", 0.5));
                    ui.setValue("vfx-lifetime", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "lifetimeSeconds", 1.0));
                } else if (isPrefabType(selectedType)) {
                    ui.setValue("prefab-lifecycle", lifecycleIndex(combatEditor.timeline.getItemPayloadText(
                        itemId, "lifecycle", "recycle_on_block_exit")).tofloat());
                    ui.setValue("prefab-duration", combatEditor.timeline.getItemPayloadNumber(
                        itemId, "customDurationSeconds", 1.0));
                }
            }
            ui.setEnabled("delete-action", true);
        } else {
            combatEditor.selectedActionId = "";
            setTypedPayloadVisibility("", false);
            ui.setText("action-block-id", "No Action Block selected");
            ui.setText("action-block-kind", "Select a block in the montage timeline");
            ui.setEnabled("action-start", false); ui.setEnabled("action-end", false);
            ui.setEnabled("action-enabled", false); ui.setEnabled("action-type", false);
            ui.setEnabled("action-payload", false); ui.setEnabled("delete-action", false);
            foreach (field in ["section-fields-title", "section-uri", "section-blend", "section-source-start",
                               "section-source-end", "section-curve"]) ui.setVisible(field, false);
        }
        ui.setEnabled("action-undo", combatEditor.timeline.canUndo());
        ui.setEnabled("action-redo", combatEditor.timeline.canRedo());
    } else if (combatEditor.inspectorMode == "track" && !combatEditor.newTrackOpen &&
               combatEditor.timeline.getTrackCount() > 0) {
        combatEditor.selectedTrack = clampf(combatEditor.selectedTrack, 0,
                                             combatEditor.timeline.getTrackCount() - 1).tointeger();
        ui.setText("track-id", combatEditor.timeline.getTrackId(combatEditor.selectedTrack));
        ui.setText("track-kind", "Kind: " + combatEditor.timeline.getTrackKind(combatEditor.selectedTrack));
        ui.setValueText("track-label", combatEditor.timeline.getTrackLabel(combatEditor.selectedTrack));
        ui.setChecked("track-muted", combatEditor.timeline.getTrackMuted(combatEditor.selectedTrack));
        ui.setChecked("track-locked", combatEditor.timeline.getTrackLocked(combatEditor.selectedTrack));
        ui.setEnabled("delete-track", combatEditor.timeline.getTrackCount() > 1);
        ui.setEnabled("track-undo", combatEditor.timeline.canUndo());
        ui.setEnabled("track-redo", combatEditor.timeline.canRedo());
    } else if (combatEditor.inspectorMode == "montage") {
        ui.setValue("montage-rate", combatEditor.timeline.getMontageBasePlayRate());
        ui.setChecked("montage-looping", combatEditor.timeline.getMontageLooping());
        ui.setChecked("montage-foot-ik", combatEditor.timeline.getMontageFootIk());
        ui.setValue("montage-layer", combatEditor.timeline.getMontageAnimationLayer().tofloat());
        ui.setValue("montage-blend-in", combatEditor.timeline.getMontageBlendIn());
        ui.setValue("montage-blend-out", combatEditor.timeline.getMontageBlendOut());
        ui.setValue("montage-blend-offset", combatEditor.timeline.getMontageBlendOutOffset());
        ui.setChecked("montage-root-horizontal", combatEditor.timeline.getMontageRootMotionHorizontal());
        ui.setChecked("montage-root-vertical", combatEditor.timeline.getMontageRootMotionVertical());
        ui.setChecked("montage-root-rotation", combatEditor.timeline.getMontageRootMotionRotation());
        local splitCount = combatEditor.timeline.getSectionSplitCount();
        combatEditor.selectedSplit = splitCount > 0 ? clampf(combatEditor.selectedSplit, 0, splitCount - 1).tointeger() : 0;
        ui.setValue("section-split-selector", combatEditor.selectedSplit.tofloat());
        ui.setValue("section-split-time", splitCount > 0 ?
            combatEditor.timeline.getSectionSplitTime(combatEditor.selectedSplit) : 0.0);
        ui.setEnabled("section-split-selector", splitCount > 0);
        ui.setEnabled("section-split-time", splitCount > 0);
        ui.setEnabled("delete-section-split", splitCount > 0);
        ui.setEnabled("montage-undo", combatEditor.timeline.canUndo());
        ui.setEnabled("montage-redo", combatEditor.timeline.canRedo());
    }
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
        local enabled = combatEditor.timeline.getItemEnabled(combatEditor.timeline.getItemId(i));
        local isState = combatEditor.timeline.getItemState(i);
        local r = selected ? 0.98 : (isState ? 0.25 : 0.86);
        local g = selected ? 0.67 : (isState ? 0.58 : 0.38);
        local b = selected ? 0.20 : (isState ? 0.92 : 0.30);
        local x0 = combatEditor.timeline.getItemMinX(i); local x1 = combatEditor.timeline.getItemMaxX(i);
        local y0 = combatEditor.timeline.getItemMinY(i) + 5.0;
        local y1 = combatEditor.timeline.getItemMaxY(i) - 5.0;
        gfx.drawSolidRect(x0, y0, x1 - x0, y1 - y0, r, g, b, enabled ? 0.95 : 0.28);
        if (combatEditor.timeline.getItemType(i) == "presentation:parameter-curve") {
            local curveId = combatEditor.timeline.getItemId(i);
            for (local sample = 0; sample <= 64; ++sample) {
                local t = sample / 64.0;
                local value = combatEditor.timeline.sampleParameterCurve(curveId, t);
                local px = x0 + t * (x1 - x0);
                local py = y1 - clampf(value, 0.0, 1.0) * (y1 - y0);
                gfx.drawSolidRect(px - 1.0, py - 1.0, 2.0, 2.0, 1.0, 0.86, 0.42, 1.0);
            }
        }
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
    handleUiEvents(); refreshDocumentToolbar(); updateTimelinePointer(); updateBoneTimelinePointer(); updatePreviewCamera(dt); updateKeyboardShortcuts();
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
