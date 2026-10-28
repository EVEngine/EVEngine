// Opt-in bounded mixing check. Leaves the resulting scene paused.
function verifyVolumeFluidMixing() {
    paused = true;
    rebuild(9);
    local initial = checked(solver.snapshot()).particles;
    for (local i = 0; i < 120; ++i) {
        checked(solver.step(1.0 / 60.0, 4));
        checked(solver.applyMaterialChannels());
    }
    local final = checked(solver.snapshot()).particles;
    if (final.len() != 250) throw "Mixing changed particle count";
    local changed = 0;
    local materialChanged = 0;
    local cohesionChanged = 0;
    for (local i = 0; i < final.len(); ++i) {
        local p = final[i];
        if (fabs(p.color[0] - initial[i].color[0]) > 0.05) changed++;
        if (fabs(p.material.viscosity - initial[i].material.viscosity) > 1.0) materialChanged++;
        if (fabs(p.material.cohesion - initial[i].material.cohesion) > 0.03) cohesionChanged++;
        if (!(p.material.viscosity >= 2.0 && p.material.viscosity <= 50.0))
            throw "Mixed viscosity outside initial range";
        if (p.material.viscosity != p.data[0]) throw "Material mapping not applied";
        if (!(p.material.cohesion >= 0.5 && p.material.cohesion <= 2.0))
            throw "Mixed surface tension outside initial range";
        if (p.material.cohesion != p.data[1]) throw "Surface tension mapping not applied";
        for (local c = 0; c < 4; ++c)
            if (!(p.color[c] >= 0.0 && p.color[c] <= 1.0)) throw "Invalid mixed color";
    }
    if (changed < 50) throw "Insufficient color transfer";
    if (materialChanged < 50) throw "Insufficient material transfer";
    if (cohesionChanged < 50) throw "Insufficient surface-tension transfer";
    dirty = true;
    return "VOLUME_FLUID_MIXING_PASS color=" + changed +
        " viscosity=" + materialChanged + " surfaceTension=" + cohesionChanged;
}
