function check(cond, msg) {
    if (!cond) throw "physics_cloth check failed: " + msg;
}

function basic() {
    local p = eve.Physics();
    check(p.getName() == "Physics", "module name");
    local clothModule = eve.Cloth();
    check(clothModule.getName() == "Cloth", "cloth module name");

    // ---- 2D cloth ----
    local c = clothModule.newCloth(8, 6, 10.0, 0.0, 0.0);
    check(c.getParticleCount() == 48, "2d particle count");
    check(c.isPinned(0), "2d top-left pinned");
    check(c.getMaxFoldAngle() == 90.0, "2d default fold angle");
    c.setGravity(0.0, 300.0);
    c.setStiffness(0.9);
    c.setIterations(5);
    c.setDamping(0.05);
    c.setParticleSize(4.0);
    check(c.getParticleSize() == 4.0, "2d particle size");
    c.setParticleMass(0.2);
    check(c.getParticleMass() == 0.2, "2d particle mass");
    c.setSelfCollision(true);
    check(c.getSelfCollision(), "2d self collision on");
    c.setFoldStiffness(0.8);
    check(c.getFoldStiffness() == 0.8, "2d fold stiffness");
    c.setMaxFoldAngle(60.0);
    check(c.getMaxFoldAngle() > 59.9 && c.getMaxFoldAngle() < 60.1, "2d fold angle roundtrip");
    c.setBounds(0.0, 0.0, 600.0, 600.0);
    c.applyForce(100.0, 0.0);
    c.interactAt(100.0, 100.0, 50.0, -1000.0);
    c.update(1.0 / 60.0);
    check(c.getParticleY(20) > 0.0, "2d particle moved");
    c.unpin(20);
    c.pin(21);
    check(c.isPinned(21) && !c.isPinned(20), "2d pin/unpin");
    local idx = c.grabAt(c.getParticleX(20), c.getParticleY(20), 20.0);
    check(idx >= 0, "2d grab");
    c.moveGrab(c.getParticleX(20) + 20.0, c.getParticleY(20) + 20.0);
    c.releaseGrab();
    check(!c.isGrabbing(), "2d grab released");

    // 2D rigid-body collision binding.
    local w = p.newWorld(0.0, 0.0, true);
    local ground = w.newBody("static", 400.0, 390.0);
    ground.newRectangleFixture(800.0, 20.0);
    c.setCollideWorld(w);
    check(c.getCollideWorld() != null, "2d collide world set");
    c.update(1.0 / 60.0);
    c.reset();
    check(c.getParticleCount() == 48, "2d reset");
    c.destroy();

    // ---- 3D cloth ----
    local c3 = clothModule.newCloth3D(8, 6, 0.5, 0.0, 3.0, 0.0);
    check(c3.getParticleCount() == 48, "3d particle count");
    check(c3.getBackendName() == "cpu" && c3.supportsFeature("runtime_tearing"), "3d backend capabilities");
    check(c3.isPinned(7), "3d top row pinned");
    check(c3.getMaxFoldAngle() == 120.0, "3d default fold angle");
    c3.setGravity(0.0, -9.8, 0.0);
    check(c3.getGravityY() == -9.8, "3d gravity");
    c3.setStiffness(0.9);
    c3.setIterations(5);
    c3.setDamping(0.02);
    c3.setCollisionMaterial(0.6, 0.1);
    c3.setCollisionFilter(1, 3);
    check(c3.getCollisionFriction() == 0.6 && c3.getCollisionMaskBits() == 3, "3d collision material/filter");
    c3.setTearThreshold(1.8);
    c3.setMaxTearsPerStep(2);
    check(c3.getTearThreshold() == 1.8 && c3.getMaxTearsPerStep() == 2, "3d tearing settings");
    c3.setParticleSize(0.12);
    c3.setStretchCompliance(0.0001);
    c3.setShearCompliance(0.0002);
    c3.setBendCompliance(0.001);
    check(c3.getStretchCompliance() > 0.00009 && c3.getShearCompliance() > 0.00019 &&
          c3.getBendCompliance() > 0.0009, "3d XPBD material compliance");
    c3.setStretchCompliance(0.0);
    c3.setShearCompliance(0.0);
    c3.setBendCompliance(0.0);
    c3.setTetherScale(1.0);
    c3.setTetherCompliance(0.0001);
    check(c3.getTetherScale() == 1.0 && c3.getTetherCompliance() > 0.00009,
          "3d tether settings");
    c3.setPressure(0.0);
    c3.setVolumeCompliance(0.001);
    check(c3.getPressure() == 0.0 && c3.getVolumeCompliance() > 0.0009,
          "3d volume settings");
    c3.setParticleMass(0.3);
    check(c3.getParticleMass() == 0.3, "3d particle mass");
    c3.setWindVelocity(4.0, 1.0, -2.0);
    check(c3.getWindVelocityX() == 4.0 && c3.getWindVelocityY() == 1.0 &&
          c3.getWindVelocityZ() == -2.0, "3d wind velocity");
    c3.setAerodynamics(1.225, 1.1, 0.25);
    check(c3.getAirDensity() > 1.224 && c3.getAirDensity() < 1.226,
          "3d air density");
    check(c3.getDragCoefficient() == 1.1 && c3.getLiftCoefficient() == 0.25,
          "3d aerodynamic coefficients");
    c3.setSelfCollision(true);
    c3.setFoldStiffness(0.8);
    c3.setMaxFoldAngle(130.0);
    check(c3.getMaxFoldAngle() > 129.9 && c3.getMaxFoldAngle() < 130.1, "3d fold angle");
    c3.setBounds(-4.0, -1.0, -3.0, 8.0, 5.5, 6.0);
    c3.applyForce(1.0, 0.0, 0.0);
    c3.interactAt(0.0, 2.0, 0.0, 1.0, -10.0);
    c3.update(1.0 / 60.0);
    check(c3.getParticleY(20) < 3.0, "3d particle fell");
    check(c3.getOriginX() == 0.0 && c3.getSpacing() == 0.5, "3d origin/spacing");

    // 3D rigid-body collision binding.
    local w3 = p.newWorld3D(0.0, -9.8, 0.0, true);
    local g3 = w3.newBody("static", 0.0, -0.5, 0.0);
    g3.newBoxShape(8.0, 1.0, 8.0);
    c3.setCollideWorld(w3);
    check(c3.getCollideWorld() != null, "3d collide world set");
    c3.update(1.0 / 60.0);

    local idx3 = c3.grabAt(c3.getParticleX(20), c3.getParticleY(20), c3.getParticleZ(20), 0.3);
    check(idx3 >= 0, "3d grab");
    c3.moveGrab(c3.getParticleX(20) + 0.2, c3.getParticleY(20) - 0.2, c3.getParticleZ(20));
    c3.releaseGrab();
    c3.setParticlePosition(21, 0.0, 2.0, 0.0);
    c3.attachParticle(21, 0.0, 2.0, 0.0, 0.0);
    check(c3.isAttached(21), "3d attachment");
    c3.updateAttachment(21, 0.1, 2.0, 0.0);
    c3.detachParticle(21);
    check(!c3.isAttached(21), "3d attachment released");
    c3.setSkinConstraint(21, 0.0, 2.0, 0.0, 0.0, 1.0, 0.0, 0.3, 0.0, 0.1, 0.0);
    check(c3.hasSkinConstraint(21), "3d skin constraint");
    c3.updateSkinReference(21, 0.0, 2.0, 0.0, 0.0, 1.0, 0.0);
    c3.clearSkinConstraint(21);
    check(c3.getParticleX(21) > -0.01 && c3.getParticleX(21) < 0.01, "3d set position");
    c3.reset();
    c3.destroy();

    // Surface-aware aerodynamics: wind normal to the initial XZ sheet moves
    // free particles while the pinned top row remains fixed.
    local windCloth = clothModule.newCloth3D(4, 4, 0.5, 0.0, 0.0, 0.0);
    windCloth.setGravity(0.0, 0.0, 0.0);
    windCloth.setSelfCollision(false);
    windCloth.setFoldStiffness(0.0);
    windCloth.setWindVelocity(0.0, 5.0, 0.0);
    windCloth.setAerodynamics(1.225, 1.0, 0.0);
    windCloth.update(1.0 / 60.0);
    check(windCloth.getParticleY(8) > 0.0, "3d aerodynamic drag moves surface");
    check(windCloth.getParticleY(0) == 0.0, "3d aerodynamics preserves pins");
    windCloth.setAerodynamics(-1.0, -2.0, -3.0);
    check(windCloth.getAirDensity() == 0.0 && windCloth.getDragCoefficient() == 0.0 &&
          windCloth.getLiftCoefficient() == 0.0, "3d aerodynamics clamps negatives");
    windCloth.destroy();
    return true;
}
