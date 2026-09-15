// Opt-in WhiskeyBottle acceptance. Loading this file is inert.
function verifyVolumeFluidWhiskeyBottle() {
    paused=true;rebuild(17);
    local initial=checked(solver.snapshot());
    if(initial.sdfColliders.len()!=1 || initial.sdfColliders[0].sdf.distances.len()==0)
        throw "Bottle model SDF was not baked";
    local sampleCount=initial.sdfColliders[0].sdf.distances.len();
    local sampleFirst=initial.sdfColliders[0].sdf.distances[0];
    local uprightOutside=0;local peakContacts=0;
    for(local frame=0;frame<150;++frame) {
        checked(solver.step(1.0/120.0,1));
        if(frame%30==29) {local contacts=checked(solver.contacts()).len();if(contacts>peakContacts)peakContacts=contacts;}
    }
    foreach(particle in checked(solver.snapshot()).particles)
        if(fluidBottleOutsideCavity(particle,0.0))++uprightOutside;
    for(local frame=0;frame<300;++frame) {
        local next=bottle.angle+0.007;if(next>2.05)next=2.05;
        local velocity=(next-bottle.angle)*120.0;bottle.angle=next;
        bottle.pose=fluidBottlePose(bottle.angle,velocity);
        checked(solver.updateSdfColliderPoses([bottle.pose]));
        checked(solver.step(1.0/120.0,1));
        if(frame%30==29) {local contacts=checked(solver.contacts()).len();if(contacts>peakContacts)peakContacts=contacts;}
    }
    for(local frame=0;frame<300;++frame) {
        checked(solver.step(1.0/120.0,1));
        if(frame%30==29) {local contacts=checked(solver.contacts()).len();if(contacts>peakContacts)peakContacts=contacts;}
    }
    local poured=0;foreach(particle in checked(solver.snapshot()).particles)
        if(fluidBottleOutsideCavity(particle,bottle.angle))++poured;
    local after=checked(solver.snapshot());
    if(after.sdfColliders[0].sdf.distances.len()!=sampleCount ||
        after.sdfColliders[0].sdf.distances[0]!=sampleFirst)
        throw "Bottle pose update rebuilt or changed SDF samples";
    if(uprightOutside!=0 || peakContacts<=0 || poured<=0)
        throw "Bottle containment/pour mismatch upright="+uprightOutside+" contacts="+peakContacts+" poured="+poured;
    dirty=true;
    return "VOLUME_FLUID_WHISKEY_BOTTLE_PASS particles="+solver.getParticleCount()+
        " samples="+sampleCount+" uprightOutside="+uprightOutside+
        " contacts="+peakContacts+" poured="+poured;
}
