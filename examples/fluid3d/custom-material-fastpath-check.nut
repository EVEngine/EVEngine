function verifyCustomMaterialFastPath() {
    paused = true;
    rebuild(0);
    dirty = false;
    checked(surface.configureMaterial(true, 0.35, 0.4, 1.25, 0.3, 0.7));
    checked(surface.configureColors(0.1, 0.2, 0.3, 0.2, 0.7, 0.9));
    surface.renderVolume(solver);
    checked(surface.copyToTexture(gfx, texture));
    checked(surface.renderVolumeColorToTexture(solver, gfx, texture));

    local fullStart = clock();
    for (local i = 0; i < 20; ++i) {
        surface.renderVolume(solver);
        checked(surface.copyToTexture(gfx, texture));
    }
    local fullMs = (clock() - fullStart) * 1000.0;
    local fastStart = clock();
    for (local i = 0; i < 20; ++i)
        checked(surface.renderVolumeColorToTexture(solver, gfx, texture));
    local fastMs = (clock() - fastStart) * 1000.0;
    if (!surface.usingGpu()) throw "custom material did not use GPU";
    if (fastMs >= fullMs) throw "device-local custom material path was not faster";
    return "VOLUME_CUSTOM_MATERIAL_FASTPATH_PASS fullMs=" + fullMs + " fastMs=" + fastMs;
}
