// Opt-in SimpleFluid numerical acceptance probe. Leaves the final surface paused.
function verifySimpleFluid() {
    paused = true;
    rebuild(0);
    local initial = checked(solver.snapshot()).particles;
    for (local frame=0; frame<120; ++frame) checked(solver.step(1.0/60.0, 2));
    local current = checked(solver.snapshot()).particles;
    local moved=0, minimumY=1000.0, maximumY=-1000.0;
    for (local i=0; i<current.len(); ++i) {
        if (fabs(current[i].position[1]-initial[i].position[1])>0.05) ++moved;
        if (current[i].position[1]<minimumY) minimumY=current[i].position[1];
        if (current[i].position[1]>maximumY) maximumY=current[i].position[1];
    }
    if (current.len()!=640 || moved<500 || minimumY<0.0 || maximumY>2.4)
        throw "SimpleFluid numerical acceptance failed";
    dirty=true;
    return "VOLUME_SIMPLE_FLUID_PASS particles="+current.len()+" moved="+moved+
        " minY="+minimumY+" maxY="+maximumY;
}
