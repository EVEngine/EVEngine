// Opt-in check for allocation-free Fluid3DEmitter.isEmitting projection.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 4;
local solver = checked(fluids.newVolumeSimulator(definition));
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 0;
emission.description.extent = [0.0, 0.0, 0.0];
local emitter = eve.VolumeFluidEmitter();
if (emitter.isEmitting()) throw "Fresh emitter reported active";
if (checked(emitter.advance(solver, emission, 0.02, 100.0, 1, 0.0)) != 1)
    throw "Emitter state probe did not emit";
if (!emitter.isEmitting() || emitter.isEmitting() != checked(emitter.snapshot()).emitting)
    throw "Direct emitter state differs from snapshot";
local saved = checked(emitter.snapshot());
saved.emitting = false;
checked(emitter.restore(saved));
if (emitter.isEmitting()) throw "Restored inactive state was ignored";
return "VOLUME_EMITTER_STATE_PASS";
