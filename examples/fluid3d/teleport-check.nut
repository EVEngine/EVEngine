// Opt-in check for the package Fluid3DActorTeleport behavior.
function verifyVolumeFluidTeleport() {
    local defaults = fluids.volumeDefaults();
    if (!defaults.ok) throw defaults.error.message;
    local created = fluids.newVolumeSimulator(defaults.value);
    if (!created.ok) throw created.error.message;
    local solver = created.value;
    local emission = fluids.volumeEmissionDefaults();
    if (!emission.ok) throw emission.error.message;
    local particle = clone emission.value.description.prototype;
    particle.position = [0.0, 1.0, 0.0];
    particle.velocity = [1.0, 2.0, 3.0];
    local admitted = solver.emit([particle]);
    if (!admitted.ok) throw admitted.error.message;
    local current = {position=[0.0,1.0,0.0], rotation=[0.0,0.0,0.0,1.0]};
    local target = {position=[1.0,1.0,0.0], rotation=[0.0,0.0,0.0,1.0]};
    local moved = solver.teleportActor(current, target);
    if (!moved.ok) throw moved.error.message;
    local state = solver.snapshot();
    if (!state.ok) throw state.error.message;
    local result = state.value.particles[0];
    if (fabs(result.position[0]-1.0)>0.00001 || fabs(result.velocity[0])>0.00001 ||
        fabs(result.velocity[1])>0.00001 || fabs(result.velocity[2])>0.00001)
        throw "Actor teleport state mismatch";
    target.position = [4.0, 1.0, 0.0];
    if (solver.teleportActor(current, target).ok) throw "Out-of-bounds teleport accepted";
    return true;
}
