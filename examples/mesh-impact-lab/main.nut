persist impactWorld = null
persist impactSession = null
persist impactTarget = null
persist impactProjectile = null
persist impactTargetVisual = null
persist impactProjectileVisual = null
persist impactCamera = null
persist impactApplied = false
persist impactFrame = 0
persist impactScreenshotSaved = false

const TARGET_TAG = 701;
const PROJECTILE_TAG = 702;

function impactRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function impactRecipe(id, width, height, depth, detail) {
    local params = impactRequire(procgen.newParams(), "new params").value;
    impactRequire(procgen.applyMeshRecipeDefaults(id, params), "recipe defaults");
    params.setFloat("width", width);
    params.setFloat("height", height);
    params.setFloat("depth", depth);
    params.setInt("detail", detail);
    return impactRequire(procgen.buildMesh(id, params), "build " + id).value;
}

function impactSubdivide(mesh) {
    local graph = impactRequire(procgen.newMeshModifierGraph(), "new modifier graph").value;
    impactRequire(graph.addNode("source", "mesh.input"), "add source");
    impactRequire(graph.addNode("subdivide", "mesh.subdivide"), "add subdivision");
    impactRequire(graph.addNode("weld", "mesh.weld"), "add weld");
    impactRequire(graph.connect("source", "subdivide", 0), "connect subdivision");
    impactRequire(graph.connect("subdivide", "weld", 0), "connect weld");
    impactRequire(graph.setNodeMesh("source", mesh), "bind subdivision source");
    impactRequire(graph.setNodeInt("subdivide", "levels", 2), "set subdivision level");
    impactRequire(graph.setNodeFloat("weld", "tolerance", 0.0001), "set weld tolerance");
    return impactRequire(graph.executeResult("weld"), "prepare damage mesh").value;
}

function impactUploadTarget() {
    local snapshot = impactRequire(impactSession.currentMeshResult(), "damage snapshot").value;
    local uploaded = impactRequire(procgen.uploadMesh(snapshot, gfx), "upload damaged mesh").value;
    impactTargetVisual.setMesh(uploaded);
}

function impactBuild() {
    local physics = eve.Physics();
    impactWorld = physics.newWorld3D(0.0, 0.0, 0.0, false);
    impactWorld.setHitEventThreshold(0.5);

    local targetMesh = impactSubdivide(impactRecipe("prototype.cylinder1", 2.8, 4.4, 2.8, 40));
    impactSession = impactRequire(procgen.newMeshDeformationSession(), "new deformation session").value;
    impactRequire(impactSession.initialize(targetMesh), "initialize target mesh");
    impactTargetVisual = eve.Renderable3D();
    impactTargetVisual.setPosition(0.0, 0.0, 0.0);
    impactTargetVisual.setTint(0.88, 0.48, 0.22, 1.0);
    impactTargetVisual.setRoughness(0.68);
    impactUploadTarget();

    impactTarget = impactWorld.newBody("static", 0.0, 2.2, 0.0);
    local targetShape = impactTarget.newBoxShape(2.8, 4.4, 2.8, 1.0, 0.35, 0.0);
    targetShape.setTag(TARGET_TAG);
    targetShape.setHitEventsEnabled(true);

    local projectileMesh = impactRecipe("prototype.sphere", 1.0, 1.0, 1.0, 24);
    impactProjectileVisual = eve.Renderable3D();
    impactProjectileVisual.setMesh(impactRequire(procgen.uploadMesh(projectileMesh, gfx), "upload projectile").value);
    impactProjectileVisual.setTint(0.32, 0.72, 0.96, 1.0);
    impactProjectileVisual.setMetallic(0.3);
    impactProjectile = impactWorld.newBody("dynamic", -6.0, 2.5, 0.0);
    local projectileShape = impactProjectile.newSphereShape(0.5, 3.0, 0.15, 0.0);
    projectileShape.setTag(PROJECTILE_TAG);
    projectileShape.setHitEventsEnabled(true);
    impactProjectile.setLinearVelocity(12.0, 0.0, 0.0);

    impactCamera = eve.Camera3D();
    impactCamera.setEye(-8.5, 6.5, -13.0);
    impactCamera.setTarget(0.0, 2.0, 0.0);
    impactCamera.setUp(0.0, 1.0, 0.0);
    impactCamera.setFov(48.0);
    impactCamera.setAmbient(0.25, 0.28, 0.34);
    impactCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.25, 1.35, 1.2, 1.05);
    gfx.setBackgroundColor(0.045, 0.06, 0.09, 1.0);
}

function impactConsumeHits() {
    for (local i = 0; i < impactWorld.getHitCount(); ++i) {
        local tagA = impactWorld.getHitShapeATag(i);
        local tagB = impactWorld.getHitShapeBTag(i);
        if (tagA != TARGET_TAG && tagB != TARGET_TAG) continue;
        local impulse = impactWorld.getHitNormalImpulse(i);
        if (impulse < 0.05) continue;
        local sign = tagA == TARGET_TAG ? -1.0 : 1.0;
        local nx = impactWorld.getHitNormalX(i) * sign;
        local ny = impactWorld.getHitNormalY(i) * sign;
        local nz = impactWorld.getHitNormalZ(i) * sign;
        impactRequire(impactSession.applyImpact(
            impactWorld.getHitPointX(i), impactWorld.getHitPointY(i), impactWorld.getHitPointZ(i),
            nx * impulse, ny * impulse, nz * impulse, 1.5, 0.012, 1.5, 0.65), "apply physics impact");
        impactUploadTarget();
        impactProjectileVisual.setVisible(false);
        impactApplied = true;
        print("MESH_IMPACT_LAB_PASS impulse=" + impulse + " colliderPolicy=deferred impact=bounded\n");
    }
}

if (impactWorld == null) impactBuild();

function eve_update(dt) {
    impactFrame += 1;
    impactWorld.updateFull(dt, 4);
    impactProjectileVisual.setPosition(impactProjectile.getX(), impactProjectile.getY(), impactProjectile.getZ());
    impactConsumeHits();
    if (impactApplied && !impactScreenshotSaved && impactFrame > 45 && gfx.saveFramePng("mesh-impact-lab.png")) {
        impactScreenshotSaved = true;
        print("mesh-impact-lab: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
