// Opt-in check for Fluid3D's independent generateRefraction setting.
function verifyRefractionToggle() {
    local localSurface = fluids.newSurfaceRenderer(64, 64);
    local configured = localSurface.configureRefraction(0.8, 4.0, 0.015, 2);
    if (!configured.ok) throw configured.error.message;
    local disabled = localSurface.configureRefractionEnabled(false);
    if (!disabled.ok) throw disabled.error.message;
    local enabled = localSurface.configureRefractionEnabled(true);
    if (!enabled.ok) throw enabled.error.message;
    return "VOLUME_REFRACTION_TOGGLE_PASS";
}
