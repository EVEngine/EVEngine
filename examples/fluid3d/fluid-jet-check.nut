// Opt-in FluidJet acceptance probe. Loading this file has no side effects.
function verifyVolumeFluidJet() {
    paused = true;
    rebuild(6);

    local emittedTotal = 0;
    local peakActive = 0;
    local emittedAfterFirstLifetime = 0;
    local sawLateralMotion = false;
    local sawTurnedVelocity = false;
    for (local frame = 0; frame < 240; ++frame) {
        local t = frame / 60.0;
        checked(solver.step(1.0 / 60.0, 2));
        local emitted = checked(emitter.advanceMoving(solver, nozzle,
            movingPose(t), movingPose(t + 1.0 / 60.0),
            1.0 / 60.0, 16, 0.5, 1.0));
        emittedTotal += emitted;
        if (frame > 100) emittedAfterFirstLifetime += emitted;

        local state = checked(solver.snapshot());
        if (state.particles.len() > peakActive) peakActive = state.particles.len();
        foreach (particle in state.particles) {
            if (abs(particle.position[0]) > 0.2) sawLateralMotion = true;
            if (abs(particle.velocity[0]) > 0.15 && abs(particle.velocity[2]) > 0.15)
                sawTurnedVelocity = true;
        }
    }

    local active = checked(solver.snapshot()).particles.len();
    if (emittedTotal <= peakActive)
        throw "Jet did not reuse expired particle capacity";
    if (emittedAfterFirstLifetime <= 0)
        throw "Jet did not resume emission after its first particle lifetime";
    if (active <= 0 || active > peakActive || peakActive >= 512)
        throw "Jet active-particle pool was not bounded";
    if (!sawLateralMotion || !sawTurnedVelocity)
        throw "Moving/rotating nozzle was not observable in emitted particles";

    return "VOLUME_FLUID_JET_PASS emitted=" + emittedTotal +
        " peak=" + peakActive + " active=" + active +
        " recycled=" + (emittedTotal - peakActive);
}
