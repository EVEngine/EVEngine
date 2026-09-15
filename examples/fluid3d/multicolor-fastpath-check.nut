function verifyMulticolorFastPath() {
    paused = true;
    rebuild(9);
    dirty = false;
    checked(surface.configureAnisotropy(true));
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
    if (!surface.usingGpu()) throw "multicolor fluid did not use GPU";
    if (fastMs >= fullMs) throw "device-local multicolor path was not faster";
    return "VOLUME_MULTICOLOR_FASTPATH_PASS fullMs=" + fullMs + " fastMs=" + fastMs;
}
