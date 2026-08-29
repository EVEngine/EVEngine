function check(cond, msg) {
    if (!cond) throw "physics_cloth check failed: " + msg;
}

function basic() {
    local p = eve.Physics();
    check(p.getName() == "Physics", "module name");

    // ---- 2D cloth ----
    local c = p.newCloth(8, 6, 10.0, 0.0, 0.0);
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
    local c3 = p.newCloth3D(8, 6, 0.5, 0.0, 3.0, 0.0);
    check(c3.getParticleCount() == 48, "3d particle count");
    check(c3.isPinned(7), "3d top row pinned");
    check(c3.getMaxFoldAngle() == 120.0, "3d default fold angle");
    c3.setGravity(0.0, -9.8, 0.0);
    check(c3.getGravityY() == -9.8, "3d gravity");
    c3.setStiffness(0.9);
    c3.setIterations(5);
    c3.setDamping(0.02);
    c3.setParticleSize(0.12);
    c3.setParticleMass(0.3);
    check(c3.getParticleMass() == 0.3, "3d particle mass");
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
    check(c3.getParticleX(21) > -0.01 && c3.getParticleX(21) < 0.01, "3d set position");
    c3.reset();
    c3.destroy();
    return true;
}
