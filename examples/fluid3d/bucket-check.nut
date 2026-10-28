// Opt-in FaucetAndBucket acceptance. Loading this file does not run it.
function verifyVolumeFluidBucket() {
    paused=true;rebuild(16);
    local emittedTotal=0;local emittedAfterLifetime=0;local peakActive=0;
    local peakContacts=0;local uprightOutside=0;local overflowObserved=0;
    for(local frame=0;frame<360;++frame) {
        if(frame>=150) {
            local next=bucket.angle+0.012;
            if(next>1.45)next=1.45;
            if(next!=bucket.angle) {
                bucket.angle=next;
                fluidBucketUpdateColliderPoses(bucket.walls,bucket.colliders,bucket.angle);
                checked(solver.setColliders(bucket.colliders));
            }
        }
        checked(solver.step(1.0/120.0,1));
        local emitted=checked(emitter.advance(solver,nozzle,1.0/120.0,2,0.25));
        emittedTotal+=emitted;if(frame>230)emittedAfterLifetime+=emitted;
        local active=solver.getParticleCount();if(active>peakActive)peakActive=active;
        local contacts=checked(solver.contacts()).len();if(contacts>peakContacts)peakContacts=contacts;
        if(frame==145 || frame>240) {
            local outside=0;
            foreach(particle in checked(solver.snapshot()).particles)
                if(fluidBucketOutside(particle,bucket.angle))++outside;
            if(frame==145)uprightOutside=outside;
            if(outside>overflowObserved)overflowObserved=outside;
        }
    }
    bucket.emitted=emittedTotal;bucket.peakActive=peakActive;dirty=true;
    if(emittedTotal<=peakActive || emittedAfterLifetime<=0 || peakActive>=64)
        throw "Bucket emission pool did not remain bounded and reusable";
    if(peakContacts<=0 || bucket.angle<1.4)
        throw "Moving bucket did not produce contacts at its poured pose";
    if(uprightOutside!=0 || overflowObserved<=0)
        throw "Bucket did not retain upright fluid then overflow while tilted";
    return "VOLUME_FLUID_BUCKET_PASS emitted="+emittedTotal+" peak="+peakActive+
        " recycled="+(emittedTotal-peakActive)+" afterLifetime="+emittedAfterLifetime+
        " contacts="+peakContacts+" overflow="+overflowObserved;
}
