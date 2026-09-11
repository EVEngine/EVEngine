// Player characters and motion sets.
//
// A character preset is a skinned model plus the mesh/skin bindings derived from
// it. A motion set is a named subset of the CC0 KayKit locomotion clips; every
// set is baked into its own MotionDatabase, because Motion Matching quality is a
// direct function of which clips are in the database.
//
// Clips are authored on the shared KayKit humanoid rig, so one clip library can
// drive every preset without a retarget profile. Records are cached per
// (character, motion set) so switching back is instant.

// Squirrel `const` only accepts scalar literals, so shared lists are functions.
function levelClipLibraryFiles() {
    return ["Rig_Medium_MovementBasic.glb", "Rig_Medium_MovementAdvanced.glb"];
}

function levelCharacterPresets() {
    return [
        {name = "Mannequin", model = "assets/kaykit/Rig_Medium_MovementBasic.glb"},
        {name = "Scout", model = "assets/kaykit/Rig_Medium_MovementAdvanced.glb"}
    ];
}

function levelCharacterNames() {
    local names = [];
    foreach (preset in levelCharacterPresets()) names.push(preset.name);
    return names;
}

function levelCharacterPreset(name) {
    foreach (preset in levelCharacterPresets()) if (preset.name == name) return preset;
    return null;
}

function levelMotionSetNames() { return ["Walk", "Run", "Locomotion", "Full"]; }

function levelMotionSetClips(name) {
    if (name == "Walk") return ["Walking_A", "Walking_B"];
    if (name == "Run") return ["Running_A", "Running_B"];
    if (name == "Full")
        return ["Walking_A", "Walking_B", "Running_A", "Running_B",
                "Running_Strafe_Left", "Running_Strafe_Right"];
    return ["Walking_A", "Walking_B", "Running_A", "Running_B"];
}

function levelActiveCharacterName() {
    local names = levelCharacterNames();
    foreach (name in names) if (name == level.characterName) return name;
    return names.len() > 0 ? names[0] : "";
}

function levelActiveMotionSetName() {
    local names = levelMotionSetNames();
    foreach (name in names) if (name == level.motionSetName) return name;
    return names.len() > 0 ? names[0] : "";
}

function levelClipLibraryModels(rig) {
    if (rig.libraryModels == null) {
        rig.libraryModels = [];
        foreach (file in levelClipLibraryFiles()) {
            local model = level.model3d.newModelDataFromFile("assets/kaykit/" + file);
            if (model != null) rig.libraryModels.push(model);
        }
    }
    return rig.libraryModels;
}

function levelFindClip(library, name) {
    foreach (model in library) {
        for (local i = 0; i < model.getAnimationCount(); ++i)
            if (model.getAnimationName(i) == name) return {model = model, index = i};
    }
    return null;
}

/** @brief Build (or fetch) the rigged character + motion database for a set. */
function levelBuildRig(characterName, motionSetName) {
    local cacheKey = characterName + "|" + motionSetName;
    if (cacheKey in level.rigCache) return level.rigCache[cacheKey];

    local preset = levelCharacterPreset(characterName);
    if (preset == null) { level.status = "unknown character " + characterName; return null; }

    local model = level.model3d.newModelDataFromFile(preset.model);
    if (model == null) { level.status = "cannot load " + preset.model; return null; }
    local skeleton = level.animation.newSkeletonFromModel(model);
    if (skeleton == null) { level.status = "no skeleton in " + preset.model; return null; }

    local rig = {
        name = characterName, motionSet = motionSetName,
        model = model, skeleton = skeleton,
        libraryModels = null, parts = [], skins = [],
        database = null, matcher = null, clipCount = 0
    };
    local library = levelClipLibraryModels(rig);

    local database = level.animation.newMotionDatabase(skeleton);
    if (database == null) { level.status = "motion database creation failed"; return null; }
    database.setRootBoneByName("root");
    database.addFeatureBoneByName("foot.l");
    database.addFeatureBoneByName("foot.r");

    local missing = [];
    foreach (clipName in levelMotionSetClips(motionSetName)) {
        local found = levelFindClip(library, clipName);
        if (found == null) { missing.push(clipName); continue; }
        local clip = level.animation.newClipFromModel(found.model, skeleton, found.index);
        if (clip == null) { missing.push(clipName); continue; }
        clip.setLoop(true);
        database.addClip(clip);
        rig.clipCount += 1;
    }
    if (rig.clipCount == 0) { level.status = "no locomotion clips found for " + motionSetName; return null; }
    database.bake();

    rig.database = database;
    rig.matcher = level.animation.newMotionMatcher(skeleton, database);
    if (rig.matcher == null) { level.status = "motion matcher creation failed"; return null; }
    rig.matcher.setSearchInterval(0.08);
    rig.matcher.setBlendTime(0.14);
    rig.matcher.setTrajectoryWeight(1.0);
    rig.matcher.setPoseWeight(0.42);
    rig.matcher.setVelocityWeight(0.8);

    for (local mesh = 0; mesh < model.getMeshCount(); ++mesh) {
        if (!model.hasBones(mesh)) continue;
        local part = level.model3d.createRenderable(gfx, model, mesh);
        part.setRoughness(0.74);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        part.setVisible(false);
        rig.parts.push(part);
        local skin = level.animation.newSkinFromModel(model, mesh, skeleton);
        if (skin != null) rig.skins.push({skin = skin, part = part});
    }
    if (rig.parts.len() == 0) { level.status = "no skinned meshes in " + preset.model; return null; }

    // The database already normalized its features, so warn once if a clip was
    // absent rather than silently shipping a thinner motion set.
    if (missing.len() > 0)
        print("level-designer: motion set '" + motionSetName + "' is missing " + missing.len() +
              " clip(s): " + missing[0] + "\n");

    level.rigCache[cacheKey] <- rig;
    return rig;
}

function levelSetCharacter(name) {
    level.characterName = name;
    level.status = "character: " + name;
}

function levelSetMotionSet(name) {
    level.motionSetName = name;
    level.status = "motion set: " + name;
}
