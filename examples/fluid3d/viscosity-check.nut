// Opt-in FluidViscosity acceptance probe. Loading this file has no side effects.
function viscosityScene(viscosity) {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.gravity=[0.0,0.0,0.0];
    definition.settings.spacing=0.1;
    definition.settings.minimum=[-0.5,0.0,-0.5];
    definition.settings.maximum=[0.5,1.2,0.5];
    definition.settings.iterations=2;
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    for(local z=0;z<4;++z)for(local y=0;y<4;++y)for(local x=0;x<4;++x) {
        local particle=clone prototype;
        particle.material=clone prototype.material;
        particle.position=[-0.15+x*0.1,0.45+y*0.1,-0.15+z*0.1];
        particle.velocity=[0.0,0.0,y<2 ? 0.5 : -0.5];
        particle.material.viscosity=viscosity;
        particle.material.yieldStress=0.0;
        particle.material.cohesion=0.0;
        definition.particles.append(particle);
    }
    return checked(fluids.newVolumeSimulator(definition));
}

function relativeFlowEnergy(particles) {
    local mean=[0.0,0.0,0.0];
    foreach(particle in particles)for(local c=0;c<3;++c)mean[c]+=particle.velocity[c];
    for(local c=0;c<3;++c)mean[c]/=particles.len();
    local energy=0.0;
    foreach(particle in particles)for(local c=0;c<3;++c) {
        local relative=particle.velocity[c]-mean[c];
        energy+=relative*relative;
    }
    return energy/particles.len();
}

function verifyVolumeFluidViscosity() {
    local low=viscosityScene(0.0),high=viscosityScene(70.0);
    local initialLow=relativeFlowEnergy(checked(low.snapshot()).particles);
    local initialHigh=relativeFlowEnergy(checked(high.snapshot()).particles);
    for(local frame=0;frame<120;++frame) {
        checked(low.step(1.0/120.0,1));
        checked(high.step(1.0/120.0,1));
    }
    local finalLow=relativeFlowEnergy(checked(low.snapshot()).particles);
    local finalHigh=relativeFlowEnergy(checked(high.snapshot()).particles);
    if(fabs(initialLow-initialHigh)>0.000001)
        throw "Viscosity controls did not start identically";
    if(finalHigh>=finalLow*0.5)
        throw "High viscosity did not materially damp relative flow";
    if(finalLow<=initialLow*0.25)
        throw "Low-viscosity control lost most relative flow";
    return "VOLUME_FLUID_VISCOSITY_PASS initial="+initialLow+
        " low="+finalLow+" high="+finalHigh+" ratio="+(finalHigh/finalLow);
}
