// Opt-in check for Fluid3D's generateSurface=false all-phase volume presentation.
function verifySurfaceDisabledVolume() {
    local defaults = fluids.volumeDefaults();
    if (!defaults.ok) throw defaults.error.message;
    local created = fluids.newVolumeSimulator(defaults.value);
    if (!created.ok) throw created.error.message;
    local localSolver = created.value;
    local emission = fluids.volumeEmissionDefaults();
    if (!emission.ok) throw emission.error.message;
    local particle = clone emission.value.description.prototype;
    particle.position = [0.0, 1.0, 0.0];
    particle.color = [0.9, 0.1, 0.05, 1.0];
    particle.material.phase = 0; // VolumeFluidPhase::Liquid
    local emitted = localSolver.emit([particle]);
    if (!emitted.ok) throw emitted.error.message;
    local localSurface = fluids.newSurfaceRenderer(64, 64);
    localSurface.setCameraXYZ(0.0, 1.0, 3.0, 0.0, 1.0, 0.0, 45.0);
    local rendererSettings = checked(fluids.fluidRendererSettingsDefaults());
    if (rendererSettings.blendSource != 5 || rendererSettings.blendDestination != 10 ||
        rendererSettings.thicknessCutoff != 1.2 || rendererSettings.thicknessDownsample != 2 ||
        rendererSettings.blurRadius != 0.02)
        throw "Fluid3D renderer defaults mismatch";
    rendererSettings.generateSurface = false;
    rendererSettings.generateReflection = false;
    rendererSettings.generateRefraction = false;
    rendererSettings.generateFoam = false;
    checked(localSurface.configureRendererSettings(rendererSettings));
    if (checked(localSurface.rendererSettings()).generateSurface)
        throw "Complete renderer settings were not applied";
    local rendered = localSurface.renderConfiguredVolume(localSolver);
    if (!rendered.ok) throw rendered.error.message;
    checked(localSurface.configureSurfaceEnabled(true));
    checked(localSurface.renderConfiguredVolume(localSolver));
    checked(localSurface.configureSurfaceEnabled(false));
    if (localSurface.renderVolumeWithoutSurface(localSolver, 0.0).ok)
        throw "Invalid surface-disabled absorption was accepted";
    return "VOLUME_SURFACE_DISABLED_PASS";
}
