// Shared FaucetAndBucket construction helpers. Loading this file is inert.
function fluidBucketPoint(localPoint, angle) {
    local c=cos(angle);local s=sin(angle);
    return [c*localPoint[0]-s*localPoint[1],0.72+s*localPoint[0]+c*localPoint[1],localPoint[2]];
}

function fluidBucketWalls() {
    return [
        {center=[-0.55,0.0,0.0],half=[0.06,0.60,0.36],label=201},
        {center=[0.55,0.0,0.0],half=[0.06,0.60,0.36],label=202},
        {center=[0.0,-0.55,0.0],half=[0.60,0.06,0.36],label=203}
    ];
}

function fluidBucketUpdateColliderPoses(walls,colliders,angle) {
    local rotation=[0.0,0.0,sin(angle*0.5),cos(angle*0.5)];
    for(local i=0;i<walls.len();++i) {
        colliders[i].center=fluidBucketPoint(walls[i].center,angle);
        colliders[i].rotation=rotation;
    }
}

function fluidBucketColliders(walls,angle) {
    local colliders=[];
    foreach(wall in walls) {
        local collider=checked(fluids.volumeColliderDefaults());
        collider.label=wall.label;collider.shape=1;collider.halfExtent=wall.half;
        collider.friction=0.12;colliders.append(collider);
    }
    fluidBucketUpdateColliderPoses(walls,colliders,angle);
    return colliders;
}

function fluidBucketScene() {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.capacity=64;definition.settings.spacing=0.09;
    definition.settings.minimum=[-1.6,-0.25,-0.45];definition.settings.maximum=[1.6,2.5,0.45];
    definition.settings.iterations=3;
    local walls=fluidBucketWalls();local colliders=fluidBucketColliders(walls,0.0);
    definition.colliders=colliders;
    return {solver=checked(fluids.newVolumeSimulator(definition)),walls=walls,colliders=colliders,
        angle=0.0,emitted=0,peakActive=0};
}

function fluidBucketOutside(particle,angle) {
    local dx=particle.position[0];local dy=particle.position[1]-0.72;
    local c=cos(angle);local s=sin(angle);
    local x=c*dx+s*dy;local y=-s*dx+c*dy;
    return fabs(x)>0.62 || y < -0.64 || (fabs(x)>0.48 && y>0.48);
}
