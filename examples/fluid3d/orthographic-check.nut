// Opt-in check for the Fluid3D-compatible orthographic SSF projection.
function verifyVolumeFluidOrthographicProjection() {
    local defaults = fluids.volumeDefaults();
    if (!defaults.ok) throw defaults.error.message;
    local created = fluids.newVolumeSimulator(defaults.value);
    if (!created.ok) throw created.error.message;
    local solver = created.value;
    local emission = fluids.volumeEmissionDefaults();
    if (!emission.ok) throw emission.error.message;
    local particle = clone emission.value.description.prototype;
    particle.position = [0.0, 1.0, 0.0];
    local emitted = solver.emit([particle]);
    if (!emitted.ok) throw emitted.error.message;

    local surface = fluids.newSurfaceRenderer(64, 64);
    local configured = surface.configureProjection(true, 1.0);
    if (!configured.ok) throw configured.error.message;
    surface.setCameraXYZ(0.0, 1.0, 3.0, 0.0, 1.0, 0.0, 45.0);
    surface.renderVolume(solver);
    if (!surface.usingGpu()) throw "Orthographic SSF did not use the GPU path";
    if (surface.configureProjection(true, 0.0).ok)
        throw "Invalid orthographic half size was accepted";
    return true;
}
