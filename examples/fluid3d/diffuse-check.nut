// Opt-in secondary-particle contract probe; no simulation/render loop.
function verifyVolumeFluidDiffuse() {
    local definition = checked(fluids.volumeDefaults());
    local p = checked(fluids.volumeEmissionDefaults()).description.prototype;
    p.position = [0.0, 1.0, 0.0];
    p.velocity = [1.0, 0.0, 0.0];
    definition.particles.append(p);
    local sim = checked(fluids.newVolumeSimulator(definition));
    local pool = eve.VolumeFluidDiffuse();
    local state = checked(pool.snapshot());
    state.capacity = 1;
    checked(pool.restore(state));
    local batch = [{position=[0.0,1.0,0.0], velocity=[0.0,0.0,0.0], life=0.025}];
    checked(pool.emit(batch));
    if (pool.emit(batch).ok) throw "Diffuse capacity exceeded";
    checked(pool.advance(sim, 0.01, 1));
    local next = checked(pool.snapshot()).particles[0];
    if (fabs(next.position[0]-0.01)>0.00001 || fabs(next.life-0.015)>0.00001)
        throw "Diffuse advection or lifetime mismatch";
    if (checked(sim.snapshot()).particles[0].position[0]!=0.0)
        throw "Diffuse advanced source fluid";
    if (pool.advance(sim, 0.01, -1).ok) throw "Invalid threshold accepted";
    state.extra <- 1;
    if (pool.restore(state).ok) throw "Unknown diffuse state field accepted";
    checked(pool.advance(sim, 0.02, 1));
    if (pool.getParticleCount()!=0 || pool.getAvailableCapacity()!=1)
        throw "Diffuse lifetime did not recycle capacity";
    return true;
}
