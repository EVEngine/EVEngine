local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, 0.0, 0.0];
definition.settings.capacity = 8;
local source = checked(fluids.newVolumeSimulator(definition));
local target = checked(fluids.newVolumeSimulator(definition));
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 0;
emission.description.extent = [0.0, 0.0, 0.0];
emission.description.speed = 2.0;
emission.description.prototype.actorGroup = 14;
local sourceEmitter = eve.VolumeFluidEmitter();
local targetEmitter = eve.VolumeFluidEmitter();
if (checked(sourceEmitter.advance(source, emission, 0.02, 120.0, 2, 0.0)) != 2)
    throw "Checkpoint source did not emit";
local checkpoint = checked(sourceEmitter.checkpoint(source, emission));
emission.description.prototype.actorGroup = 3;
checked(targetEmitter.advance(target, emission, 0.02, 60.0, 1, 0.0));
local restoredEmission = checked(targetEmitter.restoreCheckpoint(target, checkpoint));
local restored = checked(target.snapshot());
if (restored.particles.len() != 2 || restored.particles[0].actorGroup != 14 ||
    restoredEmission.description.prototype.actorGroup != 14 || !targetEmitter.isEmitting())
    throw "Combined emitter checkpoint mismatch";
print("VOLUME_FLUID_EMITTER_CHECKPOINT_PASS\n");
