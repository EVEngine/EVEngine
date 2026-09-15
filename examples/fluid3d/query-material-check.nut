// Opt-in check for the paused example. Loading this file does not run it.
function verifyVolumeFluidQueryMaterial() {
    paused = true;
    rebuild(0);
    local query = function() { return checked(solver.overlapSphere(-1.0, 0.12, -0.42, 0.01)); };
    local hits = query();
    if (hits.len() != 1) throw "Expected one particle in brush";
    local boxed = checked(solver.overlapBox(-1.0, 0.12, -0.42,
        0.01, 0.01, 0.01, 0.0, 0.0, 0.70710678, 0.70710678));
    if (boxed.len() != 1) throw "Rotated box query failed";
    if (solver.overlapBox(0.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 2.0).ok)
        throw "Invalid box rotation accepted";
    local ray = checked(solver.raycast(-1.0, 0.12, -1.0, 0.0, 0.0, 1.0, 2.0, 2, 1));
    if (ray.len()!=2 || ray[0].particleIndex!=0 || ray[0].distance<=0.0 || ray[0].normal[2]>=0.0)
        throw "Bounded ray query mismatch";
    if (ray[0].particle.position[2]!=-0.42 || ray[1].distance<=ray[0].distance)
        throw "Ray hits are not owning and distance sorted";
    if (solver.raycast(0.0,0.0,0.0,0.0,0.0,0.0,1.0,1,15).ok ||
        solver.raycast(0.0,0.0,0.0,0.0,0.0,1.0,1.0,-1,15).ok ||
        solver.raycast(0.0,0.0,0.0,0.0,0.0,1.0,1.0,1,16).ok)
        throw "Invalid ray query accepted";
    local distances = checked(solver.querySphere(-1.0,0.12,-0.42,0.0,0.02,0.0,4,1));
    if (distances.len()!=1 || distances[0].particleIndex!=0 ||
        fabs(distances[0].distance+0.08)>0.00001 ||
        fabs(distances[0].queryPoint[1]-0.14)>0.00001)
        throw "Signed sphere distance query mismatch";
    distances[0].particle.position[0]=9.0;
    if (query()[0].position[0]!=-1.0) throw "Distance query leaked mutable state";
    if (solver.querySphere(0.0,0.0,0.0,-1.0,0.0,1.0,1,15).ok ||
        solver.querySphere(0.0,0.0,0.0,1.0,-1.0,1.0,1,15).ok ||
        solver.querySphere(0.0,0.0,0.0,1.0,0.0,1.0,1,16).ok)
        throw "Invalid sphere distance query accepted";
    local boxDistances = checked(solver.queryBox(-1.0,0.12,-0.42,
        0.01,0.01,0.01, 0.0,0.0,0.70710678,0.70710678, 0.02,0.0,4,1));
    if (boxDistances.len()!=1 || boxDistances[0].particleIndex!=0 ||
        fabs(boxDistances[0].distance+0.09)>0.00001 ||
        fabs(boxDistances[0].queryPoint[1]-0.09)>0.00001 || boxDistances[0].normal[1]>=0.0)
        throw "Oriented box distance query mismatch";
    boxDistances[0].particle.position[0]=9.0;
    if (query()[0].position[0]!=-1.0) throw "Box distance query leaked mutable state";
    if (solver.queryBox(0.0,0.0,0.0,-1.0,1.0,1.0,0.0,0.0,0.0,1.0,0.0,1.0,1,15).ok ||
        solver.queryBox(0.0,0.0,0.0,1.0,1.0,1.0,0.0,0.0,0.0,2.0,0.0,1.0,1,15).ok)
        throw "Invalid box distance query accepted";
    local batch = checked(solver.queryBatch([
        {type=0,center=[-1.0,0.12,-0.42],size=[0.0,0.0,0.0],rotation=[0.0,0.0,0.0,1.0],contactOffset=0.0,maxDistance=0.0,phaseMask=1,collisionFilter=4294901761},
        {type=1,center=[-1.0,0.12,-0.42],size=[0.02,0.02,0.02],rotation=[0.0,0.0,0.70710678,0.70710678],contactOffset=0.0,maxDistance=0.0,phaseMask=1,collisionFilter=4294901761},
        {type=2,center=[-1.0,0.12,-1.0],size=[-1.0,0.12,1.0],rotation=[0.0,0.0,0.0,1.0],contactOffset=0.0,maxDistance=0.0,phaseMask=1,collisionFilter=4294901761}
    ], 2));
    if (batch.len()!=4 || batch[0].queryIndex!=0 || batch[1].queryIndex!=1 ||
        batch[2].queryIndex!=2 || batch[2].particleIndex!=0 || batch[3].particleIndex<=0)
        throw "Mixed query batch mismatch";
    checked(solver.setSimplexes([{particleIndices=[0,1,2],size=3}]));
    local simplexHits = checked(solver.querySimplexes([{
        type=0,center=[-1.0,0.12,-0.42],size=[0.0,0.0,0.0],rotation=[0.0,0.0,0.0,1.0],
        contactOffset=0.0,maxDistance=1.0,phaseMask=1,collisionFilter=4294901761
    }], 2));
    if (simplexHits.len()!=1 || simplexHits[0].simplexIndex!=0 || simplexHits[0].queryIndex!=0 ||
        fabs(simplexHits[0].simplexBary[0]+simplexHits[0].simplexBary[1]+simplexHits[0].simplexBary[2]-1.0)>0.00001)
        throw "Simplex barycentric query mismatch";
    local ellipsoidDefinition = checked(fluids.volumeDefaults());
    ellipsoidDefinition.settings.spacing = 0.2;
    local ellipsoid = checked(fluids.volumeEmissionDefaults()).description.prototype;
    ellipsoid.position = [0.0,1.0,0.0];
    ellipsoid.radii = [0.2,0.05,0.05];
    ellipsoid.orientation = [0.0,0.0,0.70710678,0.70710678];
    ellipsoidDefinition.particles = [ellipsoid];
    local ellipsoidSolver = checked(fluids.newVolumeSimulator(ellipsoidDefinition));
    local ellipsoidDistance = checked(ellipsoidSolver.querySphere(0.0,1.3,0.0,0.0,0.0,1.0,1,15));
    if (ellipsoidDistance.len()!=1 || fabs(ellipsoidDistance[0].distance-0.1)>0.00001)
        throw "Oriented ellipsoid distance mismatch";
    local ellipsoidRay = checked(ellipsoidSolver.raycast(0.0,0.7,0.0,0.0,1.0,0.0,1.0,1,15));
    if (ellipsoidRay.len()!=1 || fabs(ellipsoidRay[0].distance-0.1)>0.00001 || ellipsoidRay[0].normal[1]>=0.0)
        throw "Oriented ellipsoid ray mismatch";
    local material = clone hits[0].material;
    hits[0].material.phase = 3;
    if (query()[0].material.phase != 0) throw "Query leaked mutable state";
    material.phase = 3;
    checked(solver.paintSphere(-1.0, 0.12, -0.42, 0.01, material));
    if (query()[0].material.phase != 3) throw "Brush did not freeze particle";
    local outside = checked(solver.overlapSphere(-0.88, 0.12, -0.42, 0.01));
    if (outside.len() != 1 || outside[0].material.phase != 0) throw "Brush changed outside particle";
    material.viscosity = -1.0;
    if (solver.paintSphere(-1.0, 0.12, -0.42, 0.01, material).ok) throw "Invalid material accepted";
    if (query()[0].material.viscosity != 2.0) throw "Failed brush mutated state";
    material.viscosity = 2.0;
    material.unrecognized <- 1;
    if (solver.paintSphere(-1.0, 0.12, -0.42, 0.01, material).ok) throw "Unknown field accepted";
    delete material.unrecognized;
    material.phase = 0;
    checked(solver.paintSphere(-1.0, 0.12, -0.42, 0.01, material));
    if (query()[0].material.phase != 0) throw "Brush did not melt particle";
    if (solver.overlapSphere(0.0, 0.0, 0.0, -1.0).ok) throw "Negative radius accepted";
    local definition = checked(fluids.volumeDefaults());
    local particle = query()[0];
    particle.data = [20.0, 1.5, 0.0, 0.0];
    definition.particles = [particle];
    local mapped = checked(fluids.newVolumeSimulator(definition));
    checked(mapped.applyMaterialChannels());
    local mappedMaterial = checked(mapped.snapshot()).particles[0].material;
    if (mappedMaterial.viscosity != 20.0 || mappedMaterial.cohesion != 1.5)
        throw "Material channel mapping failed";
    local filterDefinition = checked(fluids.volumeDefaults());
    filterDefinition.settings.gravity = [0.0,0.0,0.0]; filterDefinition.settings.iterations = 2;
    local filterA = clone particle; local filterB = clone particle;
    filterA.position = [0.0,1.0,0.0]; filterB.position = [0.0,1.0,0.0];
    filterA.actorGroup = 1; filterB.actorGroup = 2;
    filterA.collisionFilter = 131073; filterB.collisionFilter = 131074;
    filterDefinition.particles = [filterA,filterB];
    local filtered = checked(fluids.newVolumeSimulator(filterDefinition)); checked(filtered.step(1.0/120.0,1));
    local filteredState = checked(filtered.snapshot()).particles;
    local filteredDelta = [filteredState[0].position[0]-filteredState[1].position[0],
        filteredState[0].position[1]-filteredState[1].position[1],filteredState[0].position[2]-filteredState[1].position[2]];
    if (filteredDelta[0]*filteredDelta[0]+filteredDelta[1]*filteredDelta[1]+filteredDelta[2]*filteredDelta[2] != 0.0)
        throw "One-way particle filters interacted";
    filterB.collisionFilter = 65538; filterDefinition.particles = [filterA,filterB];
    local reciprocal = checked(fluids.newVolumeSimulator(filterDefinition)); checked(reciprocal.step(1.0/120.0,1));
    local reciprocalState = checked(reciprocal.snapshot()).particles;
    local reciprocalDelta = [reciprocalState[0].position[0]-reciprocalState[1].position[0],
        reciprocalState[0].position[1]-reciprocalState[1].position[1],reciprocalState[0].position[2]-reciprocalState[1].position[2]];
    if (reciprocalDelta[0]*reciprocalDelta[0]+reciprocalDelta[1]*reciprocalDelta[1]+
        reciprocalDelta[2]*reciprocalDelta[2] < 0.000001)
        throw "Reciprocal particle filters did not interact";
    filterB.actorGroup = 1; filterB.collisionFilter = 131074; filterA.selfCollide = true; filterB.selfCollide = false;
    filterDefinition.particles = [filterA,filterB];
    local noSelfCollision = checked(fluids.newVolumeSimulator(filterDefinition));checked(noSelfCollision.step(1.0/120.0,1));
    local noSelfState = checked(noSelfCollision.snapshot()).particles;
    local noSelfDelta = [noSelfState[0].position[0]-noSelfState[1].position[0],
        noSelfState[0].position[1]-noSelfState[1].position[1],noSelfState[0].position[2]-noSelfState[1].position[2]];
    if (noSelfDelta[0]*noSelfDelta[0]+noSelfDelta[1]*noSelfDelta[1]+noSelfDelta[2]*noSelfDelta[2] != 0.0)
        throw "Same actor group ignored self-collision flag";
    local colliderDefinition = checked(fluids.volumeDefaults());
    colliderDefinition.settings.gravity = [0.0,0.0,0.0]; colliderDefinition.settings.iterations = 1;
    local colliderParticle = clone particle; colliderParticle.position = [0.0,1.0,0.0];
    colliderParticle.collisionFilter = 131073; colliderDefinition.particles = [colliderParticle];
    local collider = checked(fluids.volumeColliderDefaults()); collider.center = [0.0,1.0,0.0];
    collider.collisionFilter = 131074; colliderDefinition.colliders = [collider];
    local ignoredCollider = checked(fluids.newVolumeSimulator(colliderDefinition));
    checked(ignoredCollider.step(1.0/120.0,1));
    local ignoredPosition = checked(ignoredCollider.snapshot()).particles[0].position;
    local ignoredDelta = [ignoredPosition[0],ignoredPosition[1]-1.0,ignoredPosition[2]];
    if (ignoredDelta[0]*ignoredDelta[0]+ignoredDelta[1]*ignoredDelta[1]+ignoredDelta[2]*ignoredDelta[2] != 0.0)
        throw "One-way collider filter projected particle";
    collider.collisionFilter = 65538; colliderDefinition.colliders = [collider];
    local matchingCollider = checked(fluids.newVolumeSimulator(colliderDefinition));
    checked(matchingCollider.step(1.0/120.0,1));
    local matchingPosition = checked(matchingCollider.snapshot()).particles[0].position;
    local matchingDelta = [matchingPosition[0],matchingPosition[1]-1.0,matchingPosition[2]];
    if (matchingDelta[0]*matchingDelta[0]+matchingDelta[1]*matchingDelta[1]+matchingDelta[2]*matchingDelta[2] < 0.000001)
        throw "Reciprocal collider filters did not interact";
    local atmosphereDefinition = checked(fluids.volumeDefaults());
    atmosphereDefinition.settings.gravity = [0.0,0.0,0.0]; atmosphereDefinition.settings.iterations = 1;
    local atmosphereA = checked(fluids.volumeEmissionDefaults()).description.prototype;
    local atmosphereB = clone atmosphereA; atmosphereA.material = clone atmosphereA.material; atmosphereB.material = clone atmosphereB.material;
    atmosphereA.position = [-0.075,1.0,0.0]; atmosphereB.position = [0.075,1.0,0.0];
    atmosphereA.material.viscosity = 0.0; atmosphereB.material.viscosity = 0.0;
    atmosphereA.material.cohesion = 0.0; atmosphereB.material.cohesion = 0.0;
    atmosphereA.material.atmosphericPressure = 10.0; atmosphereB.material.atmosphericPressure = 10.0;
    atmosphereDefinition.particles = [atmosphereA,atmosphereB];
    local atmosphere = checked(fluids.newVolumeSimulator(atmosphereDefinition));checked(atmosphere.step(1.0/120.0,1));
    local atmosphereState = checked(atmosphere.snapshot()).particles;
    if (atmosphereState[0].velocity[0] <= 0.0 || atmosphereState[1].velocity[0] >= 0.0)
        throw "Atmospheric pressure did not compress the free surface";
    local windDefinition=checked(fluids.volumeDefaults());windDefinition.settings.gravity=[0.0,0.0,0.0];windDefinition.settings.iterations=1;
    local windParticle=checked(fluids.volumeEmissionDefaults()).description.prototype;
    windParticle.position=[0.0,1.0,0.0];windParticle.material.cohesion=0.0;
    windParticle.material.viscosity=0.0;windParticle.material.drag=60.0;windDefinition.particles=[windParticle];
    local windSolver=checked(fluids.newVolumeSimulator(windDefinition));checked(windSolver.setParticleWinds([[6.0,0.0,0.0]]));
    if (windSolver.setParticleWinds([[0.0,0.0,0.0],[0.0,0.0,0.0]]).ok) throw "Invalid wind count accepted";
    if (windSolver.step(-1.0,1).ok) throw "Invalid wind step accepted";
    checked(windSolver.step(1.0/120.0,1));local windDriven=checked(windSolver.snapshot()).particles[0].velocity[0];
    if (windDriven<=2.0 || windDriven>=3.0) throw "Relative wind drag did not accelerate particle";
    checked(windSolver.step(1.0/120.0,1));
    if (checked(windSolver.snapshot()).particles[0].velocity[0]>=windDriven) throw "Wind did not clear after successful step";
    local zoneSolver=checked(fluids.newVolumeSimulator(windDefinition));
    local ambient=checked(fluids.volumeWindZoneDefaults());ambient.direction=[0.0,1.0,0.0];ambient.intensity=2.0;
    local spherical=checked(fluids.volumeWindZoneDefaults());spherical.type=1;spherical.center=[-1.0,1.0,0.0];
    spherical.radius=2.0;spherical.intensity=4.0;spherical.radial=true;
    checked(zoneSolver.accumulateWindZones([ambient,spherical],0.0));
    local invalidZone=clone spherical;invalidZone.radius=0.0;
    if(zoneSolver.accumulateWindZones([invalidZone],0.0).ok)throw "Invalid spherical wind radius accepted";
    checked(zoneSolver.step(1.0/120.0,1));local zoneVelocity=checked(zoneSolver.snapshot()).particles[0].velocity;
    if(zoneVelocity[0]<=0.0||zoneVelocity[1]<=0.0||abs(zoneVelocity[0]/zoneVelocity[1]-1.5)>0.001)
        throw "Ambient/spherical wind-zone accumulation mismatch";
    local smoothingDefinition = checked(fluids.volumeDefaults());
    if (smoothingDefinition.version != 20) throw "Smoothing snapshot version mismatch";
    smoothingDefinition.settings.gravity = [0.0,0.0,0.0]; smoothingDefinition.settings.iterations = 1;
    local smoothingA = checked(fluids.volumeEmissionDefaults()).description.prototype;
    local smoothingB = clone smoothingA;smoothingA.material=clone smoothingA.material;smoothingB.material=clone smoothingB.material;
    smoothingA.position=[-0.125,1.0,0.0];smoothingB.position=[0.125,1.0,0.0];
    smoothingA.velocity=[1.0,0.0,0.0];smoothingB.velocity=[-1.0,0.0,0.0];
    smoothingA.material.cohesion=0.0;smoothingB.material.cohesion=0.0;
    smoothingA.material.viscosity=100.0;smoothingB.material.viscosity=100.0;
    smoothingA.material.smoothing=1.0;smoothingB.material.smoothing=4.0;
    smoothingDefinition.particles=[smoothingA,smoothingB];
    local smoothingSolver=checked(fluids.newVolumeSimulator(smoothingDefinition));checked(smoothingSolver.step(1.0/120.0,1));
    local smoothingState=checked(smoothingSolver.snapshot()).particles;
    if (smoothingState[0].velocity[0] >= 0.999 || fabs(smoothingState[0].velocity[0]+smoothingState[1].velocity[0])>0.00001)
        throw "Per-particle smoothing did not expand symmetric SPH support";
    smoothingDefinition.particles[0].material.smoothing=4.01;
    if (fluids.newVolumeSimulator(smoothingDefinition).ok) throw "Invalid smoothing accepted";
    local granularDefinition = checked(fluids.volumeDefaults());
    granularDefinition.settings.gravity = [0.0,0.0,0.0];
    local granularSolver = checked(fluids.newVolumeSimulator(granularDefinition));
    local granularEmission = checked(fluids.volumeEmissionDefaults());
    if (granularEmission.version != 10 || granularEmission.description.granularRadiusRandomness != 0.0 ||
        granularEmission.description.prototype.material.smoothing != 2.0 ||
        granularEmission.description.prototype.material.rollingContacts ||
        granularEmission.description.prototype.material.rollingFriction != 0.0 ||
        abs(granularEmission.description.prototype.material.dynamicFriction - 0.2) > 0.00001 ||
        abs(granularEmission.description.prototype.material.staticFriction - 0.2) > 0.00001 ||
        granularEmission.description.prototype.angularVelocity[0] != 0.0 ||
        granularEmission.description.prototype.angularVelocity[1] != 0.0 ||
        granularEmission.description.prototype.angularVelocity[2] != 0.0)
        throw "Granular randomness schema/default mismatch";
    granularEmission.description.origin = [0.0,1.0,0.0];
    granularEmission.description.extent = [0.0,0.0,0.0];
    granularEmission.description.prototype.material.phase = 2;
    granularEmission.description.granularRadiusRandomness = 100.0;
    checked(granularSolver.emitBurst(granularEmission,8));
    local granularParticles = checked(granularSolver.snapshot()).particles;
    local granularVaried = false;
    foreach (granularParticle in granularParticles) {
        if (granularParticle.radii[0] < 0.001 || granularParticle.radii[0] > 0.051001 ||
            granularParticle.radii[0] != granularParticle.radii[1] || granularParticle.radii[1] != granularParticle.radii[2])
            throw "Granular radius outside deterministic isotropic bounds";
        if (granularParticle.radii[0] < 0.049) granularVaried = true;
    }
    if (!granularVaried) throw "Granular radius randomness produced no variation";
    local sdfDefinition=checked(fluids.volumeDefaults());sdfDefinition.settings.gravity=[0.0,0.0,0.0];
    sdfDefinition.settings.iterations=1;
    local sdfParticle=checked(fluids.volumeEmissionDefaults()).description.prototype;
    sdfParticle.position=[0.45,0.1,0.0];sdfParticle.material=clone sdfParticle.material;
    sdfParticle.material.cohesion=0.0;sdfParticle.material.viscosity=0.0;sdfDefinition.particles=[sdfParticle];
    local sdfSolver=checked(fluids.newVolumeSimulator(sdfDefinition));
    local sdfCollider=checked(fluids.volumeSdfColliderDefaults());sdfCollider.label=88;sdfCollider.friction=0.0;
    checked(sdfSolver.setSdfColliders([sdfCollider]));checked(sdfSolver.step(1.0/120.0,1));
    local sdfState=checked(sdfSolver.snapshot());
    if(sdfState.version!=14||sdfState.sdfColliders.len()!=1||sdfState.particles[0].position[0]<=0.45)
        throw "Owned SDF collider runtime/schema mismatch";
    dirty = true;
    return true;
}
