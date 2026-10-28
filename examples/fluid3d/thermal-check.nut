// Opt-in one-step contact heating check; no render calls or callbacks.
function verifyVolumeFluidThermalContact() {
    local definition = checked(fluids.volumeDefaults());
    local particle = checked(fluids.volumeEmissionDefaults()).description.prototype;
    particle.position = [0.0, 1.25, 0.0];
    particle.data = [5.0, 1.0, 7.0, -2.0];
    definition.particles = [particle];
    local sim = checked(fluids.newVolumeSimulator(definition));
    local collider = checked(fluids.volumeColliderDefaults());
    collider.label = 17;
    checked(sim.setColliders([collider]));
    local rule = checked(fluids.volumeThermalRuleDefaults());
    rule.colliderLabel = 17;
    rule.rate = -6.0;
    checked(sim.stepWithThermalContacts(1.0 / 60.0, 4, [rule]));
    local state = checked(sim.snapshot()).particles[0];
    if (fabs(state.data[0] - 4.9) > 0.00001 || fabs(state.data[1] - 0.9) > 0.00001)
        throw "Contact heating did not apply exactly once";
    checked(sim.applyMaterialChannels());
    if (fabs(checked(sim.snapshot()).particles[0].material.viscosity - 4.9) > 0.00001)
        throw "Heated channel did not drive viscosity";
    if (sim.stepWithThermalContacts(1.0 / 60.0, 4, [rule, rule]).ok)
        throw "Duplicate thermal labels accepted";
    rule.unknown <- 1;
    if (sim.stepWithThermalContacts(1.0 / 60.0, 4, [rule]).ok)
        throw "Unknown thermal rule field accepted";
    return true;
}

// Heat and rigid feedback share one simulation step; world integration stays caller-owned.
function verifyVolumeFluidThermalCoupling() {
    local world = physics.newWorld3D(0.0, 0.0, 0.0, false);
    try {
        local body = world.newBody("dynamic", 0.0, 0.0, 0.0);
        body.newBoxShape(0.5, 0.5, 0.5, 1000.0, 0.5, 0.0);
        local bridge = eve.VolumeFluidCoupling();
        local collider = checked(fluids.volumeColliderDefaults());
        collider.label = 17;
        checked(bridge.attach(body, collider));
        local definition = checked(fluids.volumeDefaults());
        local particle = checked(fluids.volumeEmissionDefaults()).description.prototype;
        particle.position = [0.0, 1.25, 0.0];
        particle.data = [5.0, 1.0, 7.0, -2.0];
        particle.life = 1.0;
        definition.particles = [particle];
        local sim = checked(fluids.newVolumeSimulator(definition));
        local rule = checked(fluids.volumeThermalRuleDefaults());
        rule.colliderLabel = 99;
        rule.rate = -6.0;
        if (bridge.stepWithThermalContacts(world, sim, 1.0/60.0, 4, [rule]).ok)
            throw "Unlinked thermal label accepted";
        local before = checked(sim.snapshot());
        if (before.colliders.len() != 0 || before.particles[0].life != 1.0 || body.getLinearVelocityY() != 0.0)
            throw "Rejected coupling mutated state";
        rule.colliderLabel = 17;
        local contacts = checked(bridge.stepWithThermalContacts(world, sim, 1.0/60.0, 4, [rule]));
        local after = checked(sim.snapshot()).particles[0];
        if (contacts <= 0 || fabs(body.getLinearVelocityY()) < 0.000001)
            throw "Coupled thermal step omitted rigid feedback";
        if (fabs(after.data[0]-4.9) > 0.00001 || fabs(after.life-(1.0-1.0/60.0)) > 0.000001)
            throw "Heat and fluid did not share exactly one step";
        world.destroy();
        if (bridge.stepWithThermalContacts(world, sim, 1.0/60.0, 4, [rule]).ok)
            throw "Destroyed world accepted";
    } catch (error) {
        world.destroy();
        throw error;
    }
    return true;
}

// Small, bounded thermal presentation check; no render call inside the loop.
function verifyVolumeFluidThermalColors() {
    rebuild(10);
    for (local i = 0; i < 120; ++i) {
        checked(solver.stepWithThermalContacts(1.0/60.0, 4, thermalRules));
        checked(solver.applyMaterialChannelsWithColors(viscosityColors));
    }
    local particles = checked(solver.snapshot()).particles;
    if (particles.len() != 96) throw "Thermal scene count changed";
    local hot = 0.0; local cold = 0.0;
    local hotBlue = 0.0; local coldBlue = 0.0;
    for (local i = 0; i < particles.len(); ++i) {
        local p = particles[i];
        if (p.material.viscosity != p.data[0]) throw "Thermal material mapping missing";
        if (i < 48) { hot += p.material.viscosity; hotBlue += p.color[2]; }
        else { cold += p.material.viscosity; coldBlue += p.color[2]; }
    }
    if (!(hot / 48.0 < 4.9 && cold / 48.0 > 5.1 && coldBlue > hotBlue))
        throw "Hot and cold material colors did not diverge";
    paused = true; dirty = true;
    return [hot / 48.0, cold / 48.0];
}
