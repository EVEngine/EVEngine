// Opt-in check for the package AddRandomVelocity actor helper.
function verifyVolumeFluidRandomVelocity() {
    local defaults = fluids.volumeDefaults();
    if (!defaults.ok) throw defaults.error.message;
    local created = fluids.newVolumeSimulator(defaults.value);
    if (!created.ok) throw created.error.message;
    local solver = created.value;
    local emission = fluids.volumeEmissionDefaults();
    if (!emission.ok) throw emission.error.message;
    local a = clone emission.value.description.prototype;
    local b = clone emission.value.description.prototype;
    a.position = [-0.2, 1.0, 0.0]; a.velocity = [1.0, 0.0, 0.0];
    b.position = [0.2, 1.0, 0.0]; b.velocity = [0.0, 1.0, 0.0];
    local admitted = solver.emit([a, b]);
    if (!admitted.ok) throw admitted.error.message;
    local applied = solver.addRandomVelocity(5.0, 73);
    if (!applied.ok) throw applied.error.message;
    local state = solver.snapshot();
    if (!state.ok) throw state.error.message;
    local da = [state.value.particles[0].velocity[0] - 1.0,
                state.value.particles[0].velocity[1],
                state.value.particles[0].velocity[2]];
    local db = [state.value.particles[1].velocity[0],
                state.value.particles[1].velocity[1] - 1.0,
                state.value.particles[1].velocity[2]];
    for (local i = 0; i < 3; ++i)
        if (fabs(da[i] - db[i]) > 0.00001) throw "Actor impulse differs per particle";
    if (solver.addRandomVelocity(-1.0, 73).ok) throw "Invalid impulse intensity accepted";
    return true;
}
