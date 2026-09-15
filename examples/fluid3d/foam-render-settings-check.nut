// Opt-in check for Fluid3D generateFoam and foamDownsample renderer settings.
function verifyFoamRenderSettings() {
    local localSurface = fluids.newSurfaceRenderer(64, 64);
    local configured = localSurface.configureFoam(false, 4);
    if (!configured.ok) throw configured.error.message;
    configured = localSurface.configureFoam(true, 2);
    if (!configured.ok) throw configured.error.message;
    if (localSurface.configureFoam(true, 0).ok)
        throw "Invalid foam downsample was accepted";
    return "VOLUME_FOAM_RENDER_SETTINGS_PASS";
}
