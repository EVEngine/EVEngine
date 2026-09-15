// Opt-in check for Fluid3DParticlePicker + Fluid3DParticleDragger behavior.
function verifyVolumeFluidParticleDrag() {
    local definition = checked(fluids.volumeDefaults());
    definition.settings.gravity = [0.0, 0.0, 0.0];
    local particle = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
    particle.position = [0.0, 1.0, 0.0];
    particle.velocity = [1.0, 0.0, 0.0];
    definition.particles = [particle];
    local source = checked(fluids.newVolumeSimulator(definition));
    local hits = checked(source.raycast(-1.0, 1.0, 0.0, 1.0, 0.0, 0.0, 4.0, 1, 15));
    if (hits.len() != 1 || hits[0].particleIndex != 0)
        throw "Particle picker did not return the nearest particle";
    checked(source.applyParticleDrag(hits[0].particleIndex, 1.0, 1.0, 0.0, 12.0, 2.0, 0.01));
    local state = checked(source.snapshot());
    if (fabs(state.particles[0].velocity[0] - 1.1) > 0.00001)
        throw "Particle drag spring response is incorrect";
    if (source.applyParticleDrag(1, 1.0, 1.0, 0.0, 12.0, 2.0, 0.01).ok)
        throw "Stale drag index was accepted";
    return true;
}
