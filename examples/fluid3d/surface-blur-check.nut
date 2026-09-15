// Opt-in check for Fluid3D's world-space surface blur radius.
function verifySurfaceBlurRadius() {
    local localSurface = fluids.newSurfaceRenderer(64, 64);
    local configured = localSurface.configureSurfaceBlurRadius(0.02);
    if (!configured.ok) throw configured.error.message;
    if (localSurface.configureSurfaceBlurRadius(-0.01).ok)
        throw "Negative surface blur radius was accepted";
    if (localSurface.configureSurfaceBlurRadius(0.1001).ok)
        throw "Oversized surface blur radius was accepted";
    return "VOLUME_SURFACE_BLUR_PASS";
}
