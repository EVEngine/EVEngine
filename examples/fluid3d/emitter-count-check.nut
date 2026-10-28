local definition = checked(fluids.volumeDefaults());
local blueprintMetrics = checked(fluids.volumeEmitterBlueprintMetrics(8.0, 1000.0, 2.0));
if (abs(blueprintMetrics.particleSize - 0.05) > 0.000001 ||
    abs(blueprintMetrics.particleMass - 0.125) > 0.000001 ||
    abs(blueprintMetrics.smoothingRadius - 0.1) > 0.000001)
    throw "3D emitter blueprint metrics mismatch";
local fluidBlueprint = checked(fluids.volumeFluidEmitterBlueprintDefaults());
fluidBlueprint.capacity = 8;
fluidBlueprint.resolution = 8.0;
fluidBlueprint.restDensity = 875.0;
fluidBlueprint.smoothing = 3.0;
fluidBlueprint.viscosity = 0.7;
fluidBlueprint.surfaceTension = 1.5;
fluidBlueprint.buoyancy = -0.25;
fluidBlueprint.atmosphericDrag = 0.4;
fluidBlueprint.atmosphericPressure = 2.5;
fluidBlueprint.vorticity = 0.8;
fluidBlueprint.diffusion = 0.6;
fluidBlueprint.diffusionData = [1.0, 2.0, 3.0, 4.0];
local preparedBlueprint = checked(fluids.prepareVolumeFluidEmitterBlueprint3D(fluidBlueprint));
if (preparedBlueprint.solver.settings.capacity != 8 ||
    abs(preparedBlueprint.solver.settings.spacing - 0.05) > 0.000001 ||
    preparedBlueprint.emission.description.actorCapacity != 8 ||
    abs(preparedBlueprint.emission.description.prototype.material.density - 875.0) > 0.000001 ||
    abs(preparedBlueprint.emission.description.prototype.material.cohesion - 1.5) > 0.000001 ||
    abs(preparedBlueprint.emission.description.prototype.material.drag - 0.4) > 0.000001 ||
    preparedBlueprint.emission.description.prototype.data[3] != 4.0)
    throw "3D fluid emitter blueprint application mismatch";
local granularBlueprint = checked(fluids.volumeGranularEmitterBlueprintDefaults());
granularBlueprint.capacity = 7;
granularBlueprint.resolution = 8.0;
granularBlueprint.restDensity = 1450.0;
granularBlueprint.randomness = 35.0;
local preparedGranular = checked(fluids.prepareVolumeGranularEmitterBlueprint3D(granularBlueprint));
if (preparedGranular.solver.settings.capacity != 7 ||
    abs(preparedGranular.solver.settings.spacing - 0.05) > 0.000001 ||
    preparedGranular.emission.description.actorCapacity != 7 ||
    preparedGranular.emission.description.granularRadiusRandomness != 35.0 ||
    preparedGranular.emission.description.prototype.material.phase != 2 ||
    preparedGranular.emission.description.prototype.material.density != 1450.0 ||
    preparedGranular.metrics.smoothingRadius != 0.0)
    throw "3D granular emitter blueprint application mismatch";
definition.settings.gravity = [0.0, 0.0, 0.0];
definition.settings.capacity = 8;
local solver = checked(fluids.newVolumeSimulator(definition));
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 0;
emission.description.extent = [0.0, 0.0, 0.0];
emission.description.prototype.actorGroup = 21;
emission.description.actorCapacity = 2;
local emitter = eve.VolumeFluidEmitter();
if (checked(emitter.activeParticleCount(solver, emission)) != 0)
    throw "Fresh emitter actor count was not zero";
checked(emitter.advance(solver, emission, 0.02, 120.0, 2, 0.0));
if (checked(emitter.activeParticleCount(solver, emission)) != 2)
    throw "Emitter active particle count mismatch";
if (checked(emitter.advance(solver, emission, 0.02, 120.0, 2, 0.0)) != 0)
    throw "Emitter exceeded its actor capacity";
local other = checked(fluids.volumeEmissionDefaults());
other.description.shape = 0;
other.description.extent = [0.0, 0.0, 0.0];
other.description.prototype.actorGroup = 22;
other.description.actorCapacity = 1;
local otherEmitter = eve.VolumeFluidEmitter();
if (checked(otherEmitter.advance(solver, other, 0.02, 120.0, 2, 0.0)) != 1)
    throw "Independent emitter actor capacity was not available";
if (checked(emitter.activeParticleCount(solver, other)) != 1)
    throw "Independent emitter actor count mismatch";
checked(emitter.killParticle(solver, emission, 0));
if (checked(emitter.activeParticleCount(solver, emission)) != 1)
    throw "Emitter-local particle deletion did not remove exactly one particle";
if (checked(emitter.activeParticleCount(solver, other)) != 1)
    throw "Emitter-local particle deletion crossed actor ownership";
local direct = checked(fluids.volumeEmissionDefaults());
direct.description.shape = 0;
direct.description.extent = [0.0, 0.0, 0.0];
direct.description.speed = 2.0;
direct.description.actorCapacity = 1;
direct.description.prototype.actorGroup = 23;
local directEmitter = eve.VolumeFluidEmitter();
if (checked(directEmitter.emitParticle(solver, direct, 0.5, 0.02)) != 1)
    throw "Direct emitter particle was not admitted";
if (checked(directEmitter.emitParticle(solver, direct, 0.0, 0.02)) != 0)
    throw "Direct emitter particle exceeded actor capacity";
checked(solver.killActorParticles(21));
if (checked(emitter.activeParticleCount(solver, emission)) != 0)
    throw "Killed emitter actor still reports particles";
print("VOLUME_FLUID_EMITTER_COUNT_PASS\n");
