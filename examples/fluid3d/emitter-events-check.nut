function checked(result) {
    if (!result.ok) throw result.error.message;
    return result.value;
}

local defaults = checked(fluids.volumeDefaults());
defaults.settings.gravity = [0.0, 0.0, 0.0];
defaults.settings.capacity = 8;
local solver = checked(fluids.newVolumeSimulator(defaults));
checked(solver.configureParticleEvents(2));

local emission = checked(fluids.volumeEmissionDefaults());
emission.description.prototype.actorGroup = 12;
emission.description.prototype.life = 0.01;
checked(solver.emitBurst(emission, 2));
local emitted = checked(solver.drainParticleEvents());
if (emitted.events.len() != 2 || emitted.dropped != 0 ||
    emitted.events[0].type != 0 || emitted.events[0].actorGroup != 12)
    throw "Emission lifecycle events mismatch";

checked(solver.step(0.01, 2));
local killed = checked(solver.drainParticleEvents());
if (killed.events.len() != 2 || killed.dropped != 0 || killed.events[0].type != 1 ||
    killed.events[0].particle.actorGroup != 12)
    throw "Expiry lifecycle events mismatch";

print("VOLUME_FLUID_EMITTER_EVENTS_PASS\n");
