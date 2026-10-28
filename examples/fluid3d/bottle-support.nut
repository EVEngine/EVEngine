// WhiskeyBottle helpers. The mesh SDF is baked once in fluidBottleScene(); frame updates change only its pose.
const FLUID_BOTTLE_PIVOT_Y = 0.65;
const FLUID_BOTTLE_BASE_Y = 0.15;

function fluidBottlePose(angle, angularVelocity = 0.0) {
    local c=cos(angle); local s=sin(angle);
    local pose=checked(fluids.volumeSdfPoseDefaults());
    pose.label=301;
    pose.position=[s*FLUID_BOTTLE_PIVOT_Y,
        FLUID_BOTTLE_BASE_Y+FLUID_BOTTLE_PIVOT_Y-c*FLUID_BOTTLE_PIVOT_Y,0.0];
    pose.rotation=[0.0,0.0,sin(angle*0.5),cos(angle*0.5)];
    pose.angularVelocity=[0.0,0.0,angularVelocity];
    return pose;
}

function fluidBottlePoint(localPoint,angle) {
    local pose=fluidBottlePose(angle); local c=cos(angle); local s=sin(angle);
    return [pose.position[0]+c*localPoint[0]-s*localPoint[1],
        pose.position[1]+s*localPoint[0]+c*localPoint[1],localPoint[2]];
}

function fluidBottleScene() {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.capacity=48; definition.settings.spacing=0.09;
    definition.settings.minimum=[-1.8,-0.25,-0.65]; definition.settings.maximum=[1.8,2.5,0.65];
    definition.settings.iterations=3;
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    for(local z=0;z<2;++z) for(local y=0;y<3;++y) for(local x=0;x<4;++x) {
        local p=clone prototype; p.material=clone prototype.material;
        p.material.viscosity=0.8; p.material.cohesion=0.35;
        p.position=[-0.18+x*0.12,FLUID_BOTTLE_BASE_Y+0.22+y*0.12,-0.06+z*0.12];
        p.color=[0.78,0.28,0.035,0.92]; definition.particles.append(p);
    }
    local model=model3d.newModelDataFromFile("assets/whiskey-bottle.obj");
    local collider=checked(fluids.volumeSdfColliderFromModel(model,0,1.0,1.0,1.0,24));
    collider.label=301; collider.friction=0.04;
    local pose=fluidBottlePose(0.0); collider.position=pose.position; collider.rotation=pose.rotation;
    definition.sdfColliders=[collider];
    return {solver=checked(fluids.newVolumeSimulator(definition)),angle=0.0,pose=pose};
}

function fluidBottleOutsideCavity(particle,angle) {
    local pose=fluidBottlePose(angle); local dx=particle.position[0]-pose.position[0];
    local dy=particle.position[1]-pose.position[1]; local c=cos(angle); local s=sin(angle);
    local x=c*dx+s*dy; local y=-s*dx+c*dy; local radial=sqrt(x*x+particle.position[2]*particle.position[2]);
    if(y<0.08) return true;
    if(y<1.14) return radial>0.47;
    if(y<1.46) return radial>(0.47-(y-1.14)*0.75);
    return radial>0.20;
}
