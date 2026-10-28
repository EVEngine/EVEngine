// Opt-in FluidGranular acceptance probe. Loading this file has no side effects.
function granularPair(friction, rolling) {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.gravity=[0.0,0.0,0.0];
    definition.settings.iterations=1;
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    local particles=[];
    for(local i=0;i<2;++i) {
        local particle=clone prototype;
        particle.material=clone prototype.material;
        particle.position=[i==0 ? -0.045 : 0.045,1.0,0.0];
        particle.velocity=[0.0,0.0,i==0 ? 1.0 : -1.0];
        particle.material.phase=2;
        particle.material.cohesion=0.0;
        particle.material.staticFriction=friction;
        particle.material.dynamicFriction=friction;
        particle.material.rollingContacts=rolling;
        particle.material.rollingFriction=0.0;
        particles.append(particle);
    }
    definition.particles=particles;
    local sim=checked(fluids.newVolumeSimulator(definition));
    checked(sim.step(1.0/120.0,1));
    return checked(sim.snapshot()).particles;
}

function verifyVolumeFluidGranular() {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.gravity=[0.0,0.0,0.0];
    definition.settings.capacity=128;
    local sim=checked(fluids.newVolumeSimulator(definition));
    local emission=checked(fluids.volumeEmissionDefaults());
    emission.description.origin=[0.0,1.0,0.0];
    emission.description.extent=[0.0,0.0,0.0];
    emission.description.seed=91;
    emission.description.prototype.material.phase=2;
    emission.description.granularRadiusRandomness=100.0;
    checked(sim.emitBurst(emission,64));
    local particles=checked(sim.snapshot()).particles;
    local minimumRadius=1000.0,maximumRadius=0.0,varied=0;
    foreach(particle in particles) {
        local radius=particle.radii[0];
        if(radius<0.001||radius>0.051001||radius!=particle.radii[1]||radius!=particle.radii[2])
            throw "Granular radius outside deterministic isotropic bounds";
        if(radius<minimumRadius)minimumRadius=radius;
        if(radius>maximumRadius)maximumRadius=radius;
        if(radius<0.049)++varied;
    }
    if(varied<32||maximumRadius-minimumRadius<0.02)
        throw "Granular radius randomness produced insufficient variation";
    local before=sim.getParticleCount();
    emission.description.granularRadiusRandomness=100.01;
    if(sim.emitBurst(emission,1).ok||sim.getParticleCount()!=before)
        throw "Invalid granular randomness was not atomic";

    local free=granularPair(0.0,false),rough=granularPair(1.0,true);
    local freeRelative=fabs(free[0].velocity[2]-free[1].velocity[2]);
    local roughRelative=fabs(rough[0].velocity[2]-rough[1].velocity[2]);
    if(roughRelative>=freeRelative-0.1)
        throw "Granular static/dynamic friction did not reduce sliding";
    if(fabs(rough[0].angularVelocity[1])<0.1||fabs(rough[1].angularVelocity[1])<0.1)
        throw "Granular rolling contact did not generate spin";
    return "VOLUME_FLUID_GRANULAR_PASS varied="+varied+" minRadius="+minimumRadius+
        " maxRadius="+maximumRadius+" freeSlip="+freeRelative+" roughSlip="+roughRelative;
}
