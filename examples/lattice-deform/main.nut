// ============================================================================
// 3D 晶格缩放变形示例（AnimLattice）
//
// 用 3x3x3 的晶格包住一个 UV 球，演示程序化 3D 缩放变形动画：
//   1 — 整体 squash & stretch（Tween 驱动 setScale）
//   2 — 局部鼓起（控制点 scale + offset 随时间摆动）
//   3 — 波浪（按高度对 X/Z 控制点做行缩放）
//   Space — 暂停/继续   R — 重置晶格
//
// 运行：make run/<platform>-debug GAME=examples/lattice-deform
// ============================================================================

persist anim = null
persist lattice = null
persist sphereMesh = null
persist sphereObj = null
persist cam = null
persist ground = null
persist geo = null
persist mode = 1
persist animT = 0.0
persist paused = false
persist squashTween = null

const PI = 3.14159265358979;

function keyPressed(name) {
    return key_just_pressed(name);
}

function buildSphere(radius, stacks, slices) {
    local pos = [];
    local nrm = [];
    local uv = [];
    local idx = [];
    for (local i = 0; i <= stacks; i++) {
        local v = i.tofloat() / stacks;
        local phi = v * PI;
        for (local j = 0; j <= slices; j++) {
            local u = j.tofloat() / slices;
            local theta = u * 2.0 * PI;
            local x = radius * sin(phi) * cos(theta);
            local y = radius * cos(phi);
            local z = radius * sin(phi) * sin(theta);
            pos.push(x); pos.push(y); pos.push(z);
            nrm.push(x / radius); nrm.push(y / radius); nrm.push(z / radius);
            uv.push(u); uv.push(v);
        }
    }
    for (local i = 0; i < stacks; i++) {
        for (local j = 0; j < slices; j++) {
            local a = i * (slices + 1) + j;
            local b = a + 1;
            local c = (i + 1) * (slices + 1) + j;
            local d = c + 1;
            idx.push(a); idx.push(c); idx.push(b);
            idx.push(b); idx.push(c); idx.push(d);
        }
    }
    return {
        pos = pos
        nrm = nrm
        uv = uv
        idx = idx
        vc = (stacks + 1) * (slices + 1)
        ic = idx.len()
    };
}

function rebuildSphere() {
    geo = buildSphere(1.15, 26, 34);
    sphereMesh = gfx.newMeshFromArrays(geo.pos, geo.nrm, geo.uv, geo.vc, geo.idx, geo.ic);
    sphereObj.setMesh(sphereMesh);

    lattice = anim.newLattice(3, 3, 3);
    lattice.setSize(2.6, 2.6, 2.6);
    lattice.setOrigin(0.0, 0.0, 0.0);
    lattice.bindPositionsFromArray(geo.pos);
}

eve_init = function() {
    print("examples/lattice-deform ready\n");
    gfx.setBackgroundColor(0.06, 0.075, 0.09, 1.0);
    if (anim == null) anim = eve.Animation();

    if (cam == null) {
        cam = eve.Camera3D();
        cam.setEye(3.6, 2.4, 4.8);
        cam.setTarget(0.0, 0.0, 0.0);
        cam.setUp(0.0, 1.0, 0.0);
        cam.setFov(45.0);
        cam.setAmbient(0.32, 0.34, 0.38);
        cam.setActive(true);
        gfx.setDirectionalLight(-0.45, -1.0, -0.4, 1.3, 1.2, 1.05);
    }

    if (ground == null) {
        ground = eve.Renderable3D();
        ground.setMesh(gfx.newMeshCube(1.0));
        ground.setScale(8.0, 0.08, 8.0);
        ground.setPosition(0.0, -1.45, 0.0);
        ground.setTint(0.18, 0.22, 0.27, 1.0);
        ground.setCastShadow(false);
    }

    if (sphereObj == null) {
        sphereObj = eve.Renderable3D();
        sphereObj.setPosition(0.0, 0.0, 0.0);
        sphereObj.setTint(0.78, 0.52, 0.28, 1.0);
        sphereObj.setMetallic(0.15);
        sphereObj.setRoughness(0.38);
        sphereObj.setCastShadow(true);
    }

    if (geo == null || lattice == null)
        rebuildSphere();

    if (squashTween == null) {
        squashTween = anim.newTween(0.85);
        squashTween.setFrom("s", 1.0);
        squashTween.setTo("s", 0.45);
        squashTween.setEase("inOutSine");
        squashTween.setRepeat(-1);
        squashTween.setYoyo(true);
        squashTween.start();
    }
};

eve_update = function(dt) {
    if (keyPressed("space")) paused = !paused;
    if (keyPressed("r") || keyPressed("R")) {
        lattice.reset();
        mode = 1;
    }
    if (keyPressed("1")) mode = 1;
    if (keyPressed("2")) mode = 2;
    if (keyPressed("3")) mode = 3;

    if (!paused) {
        anim.update(dt);
        animT += dt;
    }

    sphereObj.setYaw(animT * 0.45);

    // --- drive the lattice control points -------------------------------
    if (mode == 1) {
        // Whole-lattice squash & stretch driven by a yoyo Tween.
        local s = squashTween.get("s");
        local inv = 1.0 / sqrt(s);
        lattice.setScale(inv, s, inv);
    } else if (mode == 2) {
        // Local bulge: the top-front corner scales out and pushes away.
        local k = 0.5 + 0.5 * sin(animT * 2.4);
        lattice.reset();
        lattice.setPointScale(2, 2, 2, 1.0 + 0.9 * k, 1.0 + 0.9 * k, 1.0 + 0.9 * k);
        lattice.setPointOffset(2, 2, 2, 0.35 * k, 0.25 * k, 0.35 * k);
    } else {
        // Horizontal wave: scale columns of control points by height.
        local wave = 0.35 * sin(animT * 2.8);
        for (local iz = 0; iz < 3; iz++) {
            local h = (iz - 1) * 0.5;  // -1 .. 1 lattice-local height
            local amp = wave * (1.0 - h * h) * 1.6;
            for (local iy = 0; iy < 3; iy++) {
                for (local ix = 0; ix < 3; ix++) {
                    lattice.setPointScale(ix, iy, iz, 1.0 + amp * sin(animT * 1.8 + ix), 1.0, 1.0 + amp * cos(animT * 1.4 + iz));
                }
            }
        }
    }

    // --- deform + upload ------------------------------------------------
    lattice.updateDeformedPositions();
    lattice.updateDeformedNormalsFromArray(lattice.getDeformedPositions(), geo.nrm);
    gfx.updateMeshVertices(sphereMesh,
                           lattice.getDeformedPositions(),
                           lattice.getDeformedNormals(),
                           [],
                           geo.vc,
                           [],
                           0);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
