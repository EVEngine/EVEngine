// Opt-in multiphase buoyancy acceptance. Runs bounded numerical scenes without rendering in the loop.
function phaseHeights(particles) {
    local blueY=0.0,redY=0.0,blueCount=0,redCount=0;
    foreach(particle in particles) {
        if(particle.color[2]>particle.color[0]) {blueY+=particle.position[1];++blueCount;}
        else {redY+=particle.position[1];++redCount;}
    }
    if(blueCount==0||redCount==0)throw "Multiphase color groups missing";
    return {blue=blueY/blueCount,red=redY/redCount};
}

function verifyMultiphaseFluidBuoyancy() {
    paused=true;
    rebuild(7);
    local initialState=checked(solver.snapshot());
    local initial=phaseHeights(initialState.particles);

    local empty=checked(fluids.volumeDefaults());
    empty.settings.capacity=initialState.settings.capacity;
    local control=checked(fluids.newVolumeSimulator(empty));
    local controlState=clone initialState;
    controlState.particles=[];
    foreach(source in initialState.particles) {
        local particle=clone source;
        particle.material=clone source.material;
        particle.material.density=1000.0;
        controlState.particles.append(particle);
    }
    checked(control.restore(controlState));

    for(local frame=0;frame<360;++frame) {
        checked(solver.step(1.0/60.0,2));
        checked(control.step(1.0/60.0,2));
    }
    local buoyant=phaseHeights(checked(solver.snapshot()).particles);
    local neutral=phaseHeights(checked(control.snapshot()).particles);
    if(buoyant.red>=buoyant.blue)
        throw "Dense phase did not sink below light phase";
    if(buoyant.red>=initial.red-0.1||buoyant.blue<=initial.blue+0.1)
        throw "Both density phases did not move in their expected relative directions";
    if(neutral.red<=neutral.blue)
        throw "Equal-density control inverted without a density contrast";
    dirty=true;
    return "VOLUME_MULTIPHASE_BUOYANCY_PASS initialLight="+initial.blue+
        " initialHeavy="+initial.red+" finalLight="+buoyant.blue+
        " finalHeavy="+buoyant.red+" neutralLight="+neutral.blue+
        " neutralHeavy="+neutral.red;
}
