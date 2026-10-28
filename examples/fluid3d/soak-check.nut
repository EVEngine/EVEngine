// Opt-in bounded stability soak. Drive volumeFluidSoakChunk() from MCP in short calls.
persist volumeFluidSoakFrames = 0;

function beginVolumeFluidSoak() {
    paused = true;
    rebuild(0);
    volumeFluidSoakFrames = 0;
    dirty = false;
    if (solver.getParticleCount() != 640)
        throw "volume fluid soak requires the 640-particle SimpleFluid scene";
    return "VOLUME_FLUID_SOAK_READY particles=640";
}

function volumeFluidSoakChunk(frameCount) {
    if (frameCount < 1 || frameCount > 10)
        throw "volume fluid soak chunk must contain 1..10 frames";
    for (local frame = 0; frame < frameCount; ++frame) {
        checked(solver.step(1.0 / 120.0, 1));
        // Periodically cover the host readback/upload path. Other frames exercise
        // the device-local uniform-color path without creating per-frame resources.
        if ((volumeFluidSoakFrames % 10) == 0) {
            surface.renderVolume(solver);
            checked(surface.copyToTexture(gfx, texture));
        } else {
            checked(surface.renderVolumeColorToTexture(solver, gfx, texture));
        }
        ++volumeFluidSoakFrames;
    }
    if (solver.getParticleCount() != 640)
        throw "volume fluid soak particle count changed";
    return "VOLUME_FLUID_SOAK_CHUNK frames=" + volumeFluidSoakFrames + " particles=640";
}
