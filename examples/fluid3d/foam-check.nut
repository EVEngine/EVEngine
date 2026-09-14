// Opt-in automatic foam generation/replay probe. Does not run a render loop.
function verifyVolumeFluidFoam() {
    local definition = checked(fluids.volumeDefaults());
    local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
    for (local i=0; i<2; ++i) {
        local p = clone prototype;
        p.position = [i==0 ? -0.05 : 0.05, 1.0, 0.0];
        p.velocity = [0.0, i==0 ? -1.0 : 1.0, 0.0];
        definition.particles.append(p);
    }
    local sim = checked(fluids.newVolumeSimulator(definition));
    local foam = eve.VolumeFluidFoam();
    local state = checked(foam.snapshot());
    state.settings.rate = 100.0;
    state.settings.vorticityThreshold = 0.0;
    state.settings.densityThreshold = 1000000.0;
    state.settings.maxPerStep = 1;
    checked(foam.restore(state));
    local pool = eve.VolumeFluidDiffuse();
    if (checked(foam.advance(sim, pool, 0.02))!=1) throw "Foam source selection failed";
    local generated = checked(pool.snapshot()).particles[0];
    local replay = eve.VolumeFluidFoam();
    local replayPool = eve.VolumeFluidDiffuse();
    checked(replay.restore(state));
    checked(replay.advance(sim, replayPool, 0.02));
    local repeated = checked(replayPool.snapshot()).particles[0];
    for(local i=0; i<3; ++i)
        if (generated.position[i]!=repeated.position[i]) throw "Foam replay mismatch";
    checked(pool.advance(sim, 0.01, 1));
    if (checked(pool.snapshot()).particles[0].life>=generated.life) throw "Foam did not advect/age";
    local agedLife = checked(pool.snapshot()).particles[0].life;
    for (local i=0; i<201; ++i) checked(pool.advance(sim, 0.01, 0));
    if (checked(pool.snapshot()).particles.len()!=0) throw "Expired foam was not returned to the pool";
    state.settings.densityThreshold = 0.0;
    checked(foam.restore(state));
    if (checked(foam.advance(sim, pool, 0.02))!=0) throw "Density filter ignored";
    state.randomState = 0;
    if (foam.restore(state).ok) throw "Invalid foam RNG accepted";
    if (foam.advance(sim, pool, -0.01).ok) throw "Invalid foam dt accepted";
    return "VOLUME_FLUID_FOAM_PASS emitted=1 agedLife=" + agedLife + " expired=1";
}
