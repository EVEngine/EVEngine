// Diffuse-particle field contract probe; does not advance or render the solver.
function verifyVolumeFluidField() {
    local definition = checked(fluids.volumeDefaults());
    local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
    for (local i = 0; i < 2; ++i) {
        local p = clone prototype;
        p.position = [i == 0 ? -0.05 : 0.05, 1.0, 0.0];
        p.velocity = [0.0, i == 0 ? -1.0 : 1.0, 0.0];
        definition.particles.append(p);
    }
    local sim = checked(fluids.newVolumeSimulator(definition));
    local samples = checked(sim.sampleField([[0.0,1.0,0.0],[100.0,100.0,100.0]]));
    if (samples.len()!=2 || samples[0].neighborCount!=2 || fabs(samples[0].vorticity[2]-8.0)>0.0001)
        throw "Field projection mismatch";
    if (samples[1].neighborCount!=0 || samples[1].density!=0.0)
        throw "Empty field support mismatch";
    if (sim.sampleField([[0.0,1.0]]).ok || sim.sampleField([[0.0,1.0,0.0,9.0]]).ok)
        throw "Malformed field positions accepted";
    if (sim.getParticleCount()!=2) throw "Field query changed solver state";
    return true;
}
