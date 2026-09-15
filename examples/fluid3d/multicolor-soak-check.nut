function verifyMulticolorSoak() {
    paused = true;
    local definition = checked(fluids.volumeDefaults());
    definition.settings.capacity = 4096;
    definition.settings.spacing = 0.05;
    definition.settings.minimum = [-0.5, 0.0, -0.5];
    definition.settings.maximum = [0.5, 1.0, 0.5];
    definition.settings.iterations = 2;
    solver = checked(fluids.newVolumeSimulator(definition));
    local red = checked(fluids.volumeEmissionDefaults());
    red.description.shape = 2;
    red.description.extent = [0.17, 0.37, 0.37];
    red.description.actorCapacity = 4096;
    red.description.origin = [-0.2, 0.5, 0.0];
    red.description.speed = 0.0;
    red.description.useShapeColor = false;
    red.description.prototype.radii = [0.0, 0.0, 0.0];
    red.description.prototype.color = [0.95, 0.08, 0.03, 1.0];
    local blue = checked(fluids.volumeEmissionDefaults());
    blue.description.shape = 2;
    blue.description.extent = [0.17, 0.37, 0.37];
    blue.description.actorCapacity = 4096;
    blue.description.origin = [0.2, 0.5, 0.0];
    blue.description.speed = 0.0;
    blue.description.useShapeColor = false;
    blue.description.prototype.radii = [0.0, 0.0, 0.0];
    blue.description.prototype.color = [0.03, 0.2, 0.95, 0.75];
    local redCount = 2048;
    local blueCount = 2048;
    checked(solver.emitBurst(red, redCount));
    checked(solver.emitBurst(blue, blueCount));
    checked(surface.configureAnisotropy(true));
    surface.setCameraXYZ(1.7, 1.5, 2.5, 0.0, 0.45, 0.0, 48.0);
    checked(surface.renderVolumeColorToTexture(solver, gfx, texture));

    local maximumMs = 0.0;
    local start = clock();
    for (local frame = 0; frame < 600; ++frame) {
        local frameStart = clock();
        checked(surface.renderVolumeColorToTexture(solver, gfx, texture));
        local frameMs = (clock() - frameStart) * 1000.0;
        if (frameMs > maximumMs) maximumMs = frameMs;
    }
    local elapsedMs = (clock() - start) * 1000.0;
    if (!surface.usingGpu()) throw "multicolor soak did not use GPU";
    if (elapsedMs > 60000.0) throw "multicolor soak exceeded bounded runtime";
    return "VOLUME_MULTICOLOR_SOAK_PASS particles=4096 frames=600 totalMs=" + elapsedMs +
        " averageMs=" + (elapsedMs / 600.0) + " maxMs=" + maximumMs;
}
