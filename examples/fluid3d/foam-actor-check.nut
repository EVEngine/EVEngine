// Opt-in check for Fluid3DFoamGenerator's owning-emitter source restriction.
function verifyVolumeFluidFoamActorSource() {
    local definition = checked(fluids.volumeDefaults());
    local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
    for (local i=0; i<4; ++i) {
        local particle = clone prototype;
        local right = i >= 2;
        local upper = (i % 2) == 1;
        particle.position = [right ? 0.55 : -0.55, upper ? 1.05 : 0.95, 0.0];
        particle.velocity = [upper ? 1.0 : -1.0, 0.0, 0.0];
        particle.actorGroup = right ? 9 : 3;
        definition.particles.append(particle);
    }
    local source = checked(fluids.newVolumeSimulator(definition));
    local foam = eve.VolumeFluidFoam();
    local state = checked(foam.snapshot());
    state.settings.rate = 100.0;
    state.settings.randomness = 0.0;
    state.settings.vorticityThreshold = 0.0;
    state.settings.densityThreshold = 1000000000.0;
    state.settings.maxPerStep = 1;
    checked(foam.restore(state));
    local pool = eve.VolumeFluidDiffuse();
    if (checked(foam.advanceFromActor(source, pool, 0.02, 9)) != 1)
        throw "Foam actor source count mismatch";
    local result = checked(pool.snapshot()).particles[0];
    if (result.position[0] < 0.4)
        throw "Foam escaped its owning emitter actor";
    checked(source.step(1.0 / 120.0, 1));
    local interpolated = eve.VolumeFluidFoam();
    checked(interpolated.restore(state));
    local interpolatedPool = eve.VolumeFluidDiffuse();
    if (checked(interpolated.advanceFromActorInterpolated(source, interpolatedPool, 0.02, 9, 0.0)) != 1)
        throw "Interpolated foam source failed";
    local oldEndpoint = checked(interpolatedPool.snapshot()).particles[0].position[0];
    if (fabs(oldEndpoint - 0.55) > 0.00001)
        throw "Foam did not use the previous fixed-step position";
    if (interpolated.advanceFromActorInterpolated(source, interpolatedPool, 0.02, 9, 1.01).ok)
        throw "Invalid foam interpolation accepted";
    if (foam.advanceFromActor(source, pool, 0.02, -1).ok)
        throw "Negative actor group accepted";
    return true;
}
