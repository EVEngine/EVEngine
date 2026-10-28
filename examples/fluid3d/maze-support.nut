// Shared FluidMaze construction and trigger helpers. Loading this file is inert.
function fluidMazePoint(localPoint, angle) {
    local c = cos(angle);
    local s = sin(angle);
    return [c * localPoint[0] - s * localPoint[1],
        1.2 + s * localPoint[0] + c * localPoint[1], localPoint[2]];
}

function fluidMazeWalls() {
    return [
        {center=[-1.05,0.0,0.0],half=[0.06,1.1,0.34],label=101},
        {center=[1.05,0.0,0.0],half=[0.06,1.1,0.34],label=102},
        {center=[0.0,1.08,0.0],half=[1.1,0.06,0.34],label=103},
        {center=[-0.70,-1.08,0.0],half=[0.40,0.06,0.34],label=104},
        {center=[0.70,-1.08,0.0],half=[0.40,0.06,0.34],label=105},
        {center=[-0.30,0.64,0.0],half=[0.72,0.055,0.34],label=106},
        {center=[0.30,0.25,0.0],half=[0.72,0.055,0.34],label=107},
        {center=[-0.30,-0.14,0.0],half=[0.72,0.055,0.34],label=108},
        {center=[0.30,-0.53,0.0],half=[0.72,0.055,0.34],label=109},
        {center=[-0.30,-0.88,0.0],half=[0.72,0.055,0.34],label=110}
    ];
}

function fluidMazeUpdateColliderPoses(walls, colliders, angle) {
    local halfAngle = angle * 0.5;
    local rotation = [0.0, 0.0, sin(halfAngle), cos(halfAngle)];
    for (local i = 0; i < walls.len(); ++i) {
        colliders[i].center = fluidMazePoint(walls[i].center, angle);
        colliders[i].rotation = rotation;
    }
}

function fluidMazeColliders(walls, angle) {
    local colliders = [];
    foreach (wall in walls) {
        local collider = checked(fluids.volumeColliderDefaults());
        collider.label = wall.label;
        collider.shape = 1;
        collider.halfExtent = wall.half;
        collider.friction = 0.08;
        colliders.append(collider);
    }
    fluidMazeUpdateColliderPoses(walls, colliders, angle);
    return colliders;
}

function fluidMazeQueries(angle) {
    local halfAngle = angle * 0.5;
    local rotation = [0.0, 0.0, sin(halfAngle), cos(halfAngle)];
    local locals = [[0.82,0.82,0.0],[-0.82,0.04,0.0],[0.0,-0.55,0.0]];
    local sizes = [[0.28,0.28,0.68],[0.28,0.28,0.68],[2.0,1.00,0.68]];
    local queries = [];
    for (local i = 0; i < locals.len(); ++i)
        queries.append({type=1,center=fluidMazePoint(locals[i],angle),size=sizes[i],rotation=rotation,
            contactOffset=0.0,maxDistance=0.0,phaseMask=1,collisionFilter=4294901761});
    return queries;
}

function fluidMazeScene() {
    local definition = checked(fluids.volumeDefaults());
    definition.settings.capacity = 128;
    definition.settings.spacing = 0.09;
    definition.settings.minimum = [-1.55,-0.25,-0.45];
    definition.settings.maximum = [1.55,2.65,0.45];
    definition.settings.iterations = 3;
    local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
    prototype.material = clone prototype.material;
    prototype.material.viscosity = 1.5;
    prototype.color = [0.08,0.62,1.0,1.0];
    for (local z = 0; z < 1; ++z) for (local y = 0; y < 3; ++y) for (local x = 0; x < 6; ++x) {
        local particle = clone prototype;
        particle.material = clone prototype.material;
        particle.position = [-0.225+x*0.09,2.02+y*0.09,0.0];
        definition.particles.append(particle);
    }
    local walls = fluidMazeWalls();
    local colliders = fluidMazeColliders(walls,0.0);
    definition.colliders = colliders;
    return {solver=checked(fluids.newVolumeSimulator(definition)),walls=walls,
        colliders=colliders,angle=0.0,angularSpeed=0.0,finished={},colored={}};
}

function fluidMazeApplyTriggers(scene, solverOverride = null) {
    local activeSolver = solverOverride == null ? scene.solver : solverOverride;
    local queries = fluidMazeQueries(scene.angle);
    local colorCounts = checked(activeSolver.applyQueryColorsPreservingOutside(
        [queries[0],queries[1]],[[1.0,0.16,0.08,1.0],[0.9,0.15,1.0,1.0]],128));
    local colorHits = checked(activeSolver.queryBatch([queries[0],queries[1]],128));
    foreach (hit in colorHits) if (hit.distance < 0.0) scene.colored[hit.particleIndex.tostring()] <- true;
    local finishHits = checked(activeSolver.queryBatch([queries[2]],128));
    local immediateFinish = 0;
    foreach (hit in finishHits) if (hit.distance < 0.0) {
        scene.finished[hit.particleIndex.tostring()] <- true;
        ++immediateFinish;
    }
    return {color0=colorCounts[0],color1=colorCounts[1],finish=immediateFinish,
        coloredTotal=scene.colored.len(),finishedTotal=scene.finished.len()};
}
