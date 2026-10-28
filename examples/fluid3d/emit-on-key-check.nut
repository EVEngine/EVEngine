// Opt-in check for EmitOnKeyPress/FluidJetController zero-speed behavior.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 16;
local localSolver = checked(fluids.newVolumeSimulator(definition));
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 0;
emission.description.extent = [0.0, 0.0, 0.0];
local stream = eve.VolumeFluidEmitter();
if (checked(stream.advance(localSolver, emission, 0.006, 100.0, 1, 0.0)) != 0)
    throw "Fractional stream emitted too early";
emission.description.speed = 0.0;
for (local i = 0; i < 20; ++i)
    if (checked(stream.advance(localSolver, emission, 1.0 / 30.0, 100.0, 1, 0.0)) != 0)
        throw "Disabled emitter produced particles";
if (localSolver.getParticleCount() != 0 || checked(stream.snapshot()).phase != "0")
    throw "Disabled emitter retained catchup debt";
emission.description.speed = 1.0;
if (checked(stream.advance(localSolver, emission, 0.006, 100.0, 1, 0.0)) != 0 ||
    checked(stream.advance(localSolver, emission, 0.006, 100.0, 1, 0.0)) != 1)
    throw "Re-enabled emitter did not restart from zero credit";
return "VOLUME_EMIT_ON_KEY_PASS";
