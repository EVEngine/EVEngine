// Opt-in owning particle-pool lifecycle check.
function verifyVolumeFluidLifecycle() {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.gravity=[0.0,0.0,0.0];
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    local particles=[];
    for(local i=0;i<3;++i) {
        local particle=clone prototype;
        particle.position=[-0.2+i*0.2,1.0,0.0];
        particles.append(particle);
    }
    local testSolver=checked(fluids.newVolumeSimulator(definition));
    checked(testSolver.emit(particles));
    checked(testSolver.killParticle(1));
    local state=checked(testSolver.snapshot());
    if(testSolver.getParticleCount()!=2||state.particles.len()!=2||
       fabs(state.particles[0].position[0]+0.2)>0.00001||
       fabs(state.particles[1].position[0]-0.2)>0.00001)
        throw "Particle kill did not compact the owning pool";
    local invalid=testSolver.killParticle(2);
    if(invalid.ok||testSolver.getParticleCount()!=2)
        throw "Stale particle kill was not atomic";
    testSolver.clear();
    if(testSolver.getParticleCount()!=0)throw "Kill-all clear failed";
    return true;
}
