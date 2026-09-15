// Opt-in real-renderer check for Fluid3D renderablePositions timing.
function verifyVolumeFluidSurfaceInterpolation() {
    local definition = checked(fluids.volumeDefaults());
    definition.settings.gravity = [0.0, 0.0, 0.0];
    local particle = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
    particle.position = [-0.5, 1.0, 0.0];
    particle.velocity = [60.0, 0.0, 0.0];
    definition.particles.append(particle);
    local source = checked(fluids.newVolumeSimulator(definition));
    checked(source.step(1.0 / 120.0, 1));
    local renderer = surface;
    checked(renderer.configureProjection(true, 1.0));
    renderer.setCameraXYZ(0.0, 1.0, 3.0, 0.0, 1.0, 0.0, 48.0);
    checked(renderer.prepare());
    checked(renderer.renderVolumeInterpolated(source, 0.0));
    checked(renderer.renderVolumeInterpolated(source, 1.0));
    if (renderer.renderVolumeInterpolated(source, -0.01).ok)
        throw "Invalid surface interpolation accepted";
    checked(renderer.renderVolumeInterpolated(source, 0.0));
    checked(renderer.copyToImage(pixels));
    gfx.updateTextureFromImageData(texture, pixels);
    paused = true;
    dirty = false;
    return true;
}
