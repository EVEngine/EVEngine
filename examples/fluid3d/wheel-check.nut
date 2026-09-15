// Load from the paused volume-fluid example with dofile("wheel-check.nut").
// This defines an opt-in bounded check; loading alone performs no simulation.
function verifyVolumeFluidWheel() {
    paused = true;
    local run = function(offset, ticks, emitWater) {
        rebuild(8);
        nozzle.description.origin[0] = offset;
        for (local i = 0; i < ticks; ++i) {
            wheelContacts += checked(wheelCoupling.step(wheelWorld, solver, 1.0 / 60.0, 4));
            wheelWorld.update(1.0 / 60.0);
            if (emitWater) advanceWheelJet(1.0 / 60.0);
        }
        return { speed = wheelBody.getAngularVelocityZ(), contacts = wheelContacts,
                 particles = solver.getParticleCount() };
    };
    try {
        local dry = run(0.25, 60, false);
        if (!(fabs(dry.speed) <= 0.00001) || dry.contacts != 0 || dry.particles != 0)
            throw "Dry wheel moved or produced fluid contacts";
        local right = run(0.25, 240, true);
        if (!(right.speed < -0.001 && right.speed > -100.0) || right.contacts == 0)
            throw "Right jet did not drive a finite clockwise wheel motion";
        local left = run(-0.25, 240, true);
        if (!(left.speed > 0.001 && left.speed < 100.0) || left.contacts == 0)
            throw "Left jet did not reverse wheel motion";
        dirty = true;
        return { dry = dry, right = right, left = left };
    } catch (error) {
        failure = error.tostring();
        dirty = true;
        throw error;
    }
}
