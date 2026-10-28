// Bounded Karman wake helper. Loading this file is inert.
function fluidKarmanScene() {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.capacity=192;definition.settings.spacing=0.085;
    definition.settings.gravity=[0.0,0.0,0.0];
    definition.settings.minimum=[-1.3,0.15,-0.14];definition.settings.maximum=[1.3,1.45,0.14];
    definition.settings.iterations=2;
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    for(local y=0;y<10;++y) for(local x=0;x<18;++x) {
        local p=clone prototype;p.material=clone prototype.material;
        p.material.viscosity=0.08;p.material.cohesion=0.0;p.material.vorticity=0.2;
        p.position=[-1.12+x*0.09,0.36+y*0.09,((x+y)%2 ? -0.018 : 0.018)];
        p.velocity=[1.05,0.025*sin(y*2.3+x*0.7),0.0];
        p.color=[0.04,0.42+0.025*y,0.95,0.92];
        definition.particles.append(p);
    }
    local obstacle=checked(fluids.volumeColliderDefaults());
    obstacle.label=401;obstacle.shape=0;obstacle.center=[-0.22,0.765,0.0];
    obstacle.radius=0.19;obstacle.friction=0.02;
    definition.colliders=[obstacle];
    return {solver=checked(fluids.newVolumeSimulator(definition)),obstacle=obstacle,
        peakContacts=0,peakWakeCurl=0.0};
}

function fluidKarmanSampleWake(scene,fluid) {
    local points=[];
    foreach(x in [0.08,0.28,0.48,0.68]) foreach(y in [0.50,0.63,0.765,0.90,1.03])
        points.append([x,y,0.0]);
    local samples=checked(fluid.sampleField(points));
    local positive=0;local negative=0;local peak=0.0;
    foreach(sample in samples) {
        local curl=sample.vorticity[2];if(curl>0.1)++positive;if(curl < -0.1)++negative;
        if(fabs(curl)>peak)peak=fabs(curl);
    }
    if(peak>scene.peakWakeCurl)scene.peakWakeCurl=peak;
    return {positive=positive,negative=negative,peak=peak};
}
