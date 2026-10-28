// Opt-in checks for the reference ColorFromVelocity and ColorFromPhase utilities.
function verifyVolumeFluidColorizers() {
    local solver=checked(fluids.newVolumeSimulator(checked(fluids.volumeDefaults())));
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    local a=clone prototype;local b=clone prototype;
    a.position=[0.0,1.0,0.0];a.velocity=[-0.2,0.0,0.2];a.actorGroup=0;
    b.position=[0.2,1.0,0.0];b.velocity=[0.2,-0.2,0.0];b.actorGroup=1;
    checked(solver.emit([a,b]));checked(solver.applyVelocityColors(0.2));
    local state=checked(solver.snapshot());
    if(state.particles[0].color[0]>0.001||state.particles[0].color[2]<0.999)
        throw "ColorFromVelocity mapping mismatch";
    checked(solver.applyActorGroupColors());state=checked(solver.snapshot());
    if(state.particles[0].color[0]<0.94||state.particles[1].color[2]<0.86)
        throw "ColorFromPhase alphabet mismatch";
    local gradient=[{coordinate=0.0,color=[1.0,0.0,0.0,1.0]},
                    {coordinate=1.0,color=[0.0,0.0,1.0,1.0]}];
    checked(solver.applyDataColors(0,gradient));state=checked(solver.snapshot());
    if(state.particles[0].color[0]<0.999)throw "Fluid property colorizer mismatch";
    checked(solver.applyRandomColors(gradient,91));local first=checked(solver.snapshot());
    checked(solver.applyRandomColors(gradient,91));local second=checked(solver.snapshot());
    if(fabs(first.particles[0].color[0]-second.particles[0].color[0])>0.000001)
        throw "Seeded ColorRandomizer mismatch";
    return true;
}
