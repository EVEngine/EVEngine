// Opt-in bounded solid attachment check, no rendering inside the loop.
function verifyVolumeFluidAttachments() {
    rebuild(11);
    advanceAttachment(0.0);
    local initial = checked(solver.snapshot());
    if (initial.version != 20 || initial.attachments.len() != 16) throw "Missing solid attachments";
    for (local frame = 1; frame <= 60; ++frame) advanceAttachment(frame / 60.0);
    local final = checked(solver.snapshot());
    if (final.attachments.len() != 16) throw "Attachment count changed";
    local angle = 0.35 * sin(1.0);
    foreach (anchor in final.attachments) {
        local localPoint = anchor.localPosition;
        local p = final.particles[anchor.particleIndex];
        if (fabs(p.color[0] - 0.95) > 0.00001 || fabs(p.color[1] - 0.65) > 0.00001 || fabs(p.color[2] - 0.2) > 0.00001)
            throw "Solid color not applied";
        local expectedX = 0.3 * sin(1.0) + cos(angle)*localPoint[0] - sin(angle)*localPoint[1];
        local expectedY = 0.5 + sin(angle)*localPoint[0] + cos(angle)*localPoint[1];
        if (fabs(p.position[0]-expectedX) > 0.00001 || fabs(p.position[1]-expectedY) > 0.00001)
            throw "Solid attachment lost local point";
        local localOrientation = anchor.localOrientation;
        local expectedZ = sin(angle * 0.5) * localOrientation[3] + cos(angle * 0.5) * localOrientation[2];
        local expectedW = cos(angle * 0.5) * localOrientation[3] - sin(angle * 0.5) * localOrientation[2];
        if (fabs(p.orientation[2]-expectedZ) > 0.00001 || fabs(p.orientation[3]-expectedW) > 0.00001)
            throw "Solid attachment lost local orientation";
    }
    checked(solver.restore(final));
    if (checked(solver.snapshot()).attachments.len() != 16) throw "Attachment restore failed";

    local emptyDefinition=checked(fluids.volumeDefaults());
    emptyDefinition.settings.capacity=final.settings.capacity;
    local colliderFirst=checked(fluids.newVolumeSimulator(emptyDefinition));
    checked(colliderFirst.restore(final));
    checked(colliderFirst.setColliders([]));
    local withoutCollider=checked(colliderFirst.snapshot());
    if(withoutCollider.attachments.len()!=0||withoutCollider.particles.len()!=final.particles.len())
        throw "Collider-first destruction left stale attachments or removed particles";

    local particlesFirst=checked(fluids.newVolumeSimulator(emptyDefinition));
    checked(particlesFirst.restore(final));
    particlesFirst.clear();
    local withoutParticles=checked(particlesFirst.snapshot());
    if(withoutParticles.attachments.len()!=0||withoutParticles.particles.len()!=0)
        throw "Particle-first destruction left stale attachments";
    checked(particlesFirst.setColliders([]));
    paused = true; dirty = true;
    return "VOLUME_SOLID_ATTACHMENT_PASS attached="+final.attachments.len()+" colliderFirst=0 particleFirst=0";
}

// Seed from an actual collider contact, then freeze one adjoining layer per call.
function verifyVolumeFluidPropagation() {
    local definition = checked(fluids.volumeDefaults());
    definition.settings.gravity = [0.0, 0.0, 0.0];
    local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
    prototype.material.viscosity = 0.0; prototype.material.cohesion = 0.0;
    for (local i = 0; i < 4; ++i) {
        local p = clone prototype;
        p.position = [0.0, 1.2999 + i * 0.1, 0.0];
        definition.particles.append(p);
    }
    local sim = checked(fluids.newVolumeSimulator(definition));
    local collider = checked(fluids.volumeColliderDefaults());
    collider.label = 17; collider.solidify = true;
    checked(sim.setColliders([collider]));
    for (local wave = 0; wave < 3; ++wave) {
        checked(sim.step(1.0 / 120.0, 1));
        checked(sim.applySolidColor(0.95, 0.65, 0.2, 1.0));
        if (checked(sim.snapshot()).attachments.len() != wave + 2)
            throw "Solid contact propagation did not advance one layer";
    }
    collider.center[0] = 0.1;
    checked(sim.setColliders([collider]));
    checked(sim.step(1.0 / 120.0, 1));
    foreach (p in checked(sim.snapshot()).particles)
        if (fabs(p.position[0] - 0.1) > 0.00001) throw "Propagated solid did not follow collider";
    return "VOLUME_SOLID_PROPAGATION_PASS waves=4 moved=4 colored=4";
}

function verifySolidifyOnContact() {
    local attachment=verifyVolumeFluidAttachments();
    local propagation=verifyVolumeFluidPropagation();
    return attachment+" | "+propagation;
}
