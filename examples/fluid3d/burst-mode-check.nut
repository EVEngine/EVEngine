// Opt-in check for Fluid3DEmitter EmissionMethod.BURST lifecycle behavior.
function verifyVolumeFluidBurstMode() {
    local defaults = fluids.volumeDefaults();
    if (!defaults.ok) throw defaults.error.message;
    defaults.value.settings.capacity = 8;
    defaults.value.settings.gravity = [0.0, 0.0, 0.0];
    local created = fluids.newVolumeSimulator(defaults.value);
    if (!created.ok) throw created.error.message;
    local emission = fluids.volumeEmissionDefaults();
    if (!emission.ok) throw emission.error.message;
    emission.value.description.prototype.actorGroup = 9;
    emission.value.description.prototype.life = 0.01;
    local emitter = eve.VolumeFluidEmitter();
    local first = emitter.advanceBurst(created.value, emission.value, 2, 0.0);
    if (!first.ok || first.value != 2) throw "Initial burst failed";
    local waiting = emitter.advanceBurst(created.value, emission.value, 2, 0.0);
    if (!waiting.ok || waiting.value != 0) throw "Burst repeated while actor particles were alive";
    local killed = created.value.killParticle(1);
    if (!killed.ok) throw killed.error.message;
    killed = created.value.killParticle(0);
    if (!killed.ok) throw killed.error.message;
    local repeated = emitter.advanceBurst(created.value, emission.value, 2, 0.0);
    if (!repeated.ok || repeated.value != 2) throw "Burst did not resume after actor particles expired";
    if (emitter.advanceBurst(created.value, emission.value, 0, 0.0).ok)
        throw "Invalid burst count accepted";
    return true;
}
