// Opt-in check for Fluid3D's independent generateReflection setting.
function verifyReflectionToggle() {
    local localSurface = fluids.newSurfaceRenderer(64, 64);
    local material = localSurface.configureMaterial(false, 0.0, 0.0, 0.0, 0.85, 1.0);
    if (!material.ok) throw material.error.message;
    local disabled = localSurface.configureReflection(false);
    if (!disabled.ok) throw disabled.error.message;
    local enabled = localSurface.configureReflection(true);
    if (!enabled.ok) throw enabled.error.message;
    return "VOLUME_REFLECTION_TOGGLE_PASS";
}
