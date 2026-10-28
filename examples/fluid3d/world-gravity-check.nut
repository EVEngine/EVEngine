// Opt-in check for the package WorldSpaceGravity component behavior.
function verifyVolumeFluidWorldGravity() {
    local definition = checked(fluids.volumeDefaults());
    definition.settings.gravity = [0.0, 0.0, 0.0];
    local particle = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
    particle.position = [0.0, 1.0, 0.0];
    definition.particles.append(particle);
    local source = checked(fluids.newVolumeSimulator(definition));
    checked(source.setGravity(0.0, 9.81, 0.0));
    checked(source.step(1.0 / 120.0, 1));
    local state = checked(source.snapshot());
    if (state.particles[0].velocity[1] <= 0.0 || state.settings.gravity[1] != 9.81)
        throw "Dynamic world gravity was not applied";
    if (source.setGravity(1001.0, 0.0, 0.0).ok)
        throw "Out-of-range gravity accepted";
    local unchanged = checked(source.snapshot());
    if (unchanged.settings.gravity[1] != 9.81)
        throw "Invalid gravity changed solver settings";
    return true;
}
