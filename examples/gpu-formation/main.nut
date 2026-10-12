// GPU Formation — pure-script squad layout powered by eve.TF().
//
//   eve run examples/gpu-formation
//
// Showcases the tensor package without touching C++:
//   • formation templates built with arange / sin / cos / reshape / concat
//   • homogeneous matmul  [N,3] @ [3,2]  → world slot positions
//   • tf.func() + compile() once → GPU (or CPU fallback) soft-pursuit step
//   • morph blend between formations via broadcast arithmetic
//
// Controls:
//   1–5   switch formation (grid / wedge / circle / line / column)
//   WASD  move leader     Q/E rotate
//   Space pause           R reset units onto current slots

persist tf = null
persist stepFn = null
persist positions = null
persist offsets = null
persist offsetsFrom = null
persist offsetsTo = null
persist morphT = 1.0
persist leaderX = 480.0
persist leaderY = 320.0
persist heading = 0.0
persist formation = 0
persist paused = false
persist frame = 0
persist deviceName = "unknown"
persist autoDemo = true

const UNIT_COUNT = 128;
const PURSUIT_GAIN = 0.18;
const LEADER_SPEED = 160.0;
const TURN_SPEED = 2.2;
const MORPH_SPEED = 1.6;
const SLOT_SPACING = 28.0;
const TWO_PI = 6.28318530718;

function stackXY1(tf, xs, ys) {
    // xs, ys: rank-1 length N → homogeneous [N,3] = [x, y, 1]
    local n = xs.getDim0();
    local xCol = tf.reshape2(xs, n, 1);
    local yCol = tf.reshape2(ys, n, 1);
    local ones = tf.reshape2(tf.ones1(n), n, 1);
    return tf.concat3(xCol, yCol, ones, 1);
}

function makeGrid(tf, n, spacing) {
    local cols = 16;
    local rows = (n + cols - 1) / cols;
    local xs = tf.zeros1(n);
    local ys = tf.zeros1(n);
    for (local i = 0; i < n; i += 1) {
        local c = i % cols;
        local r = i / cols;
        xs.set1(i, (c - (cols - 1) * 0.5) * spacing);
        ys.set1(i, (r - (rows - 1) * 0.5) * spacing);
    }
    return stackXY1(tf, xs, ys);
}

function makeWedge(tf, n, spacing) {
    local xs = tf.zeros1(n);
    local ys = tf.zeros1(n);
    // tip at +X; ranks fan left/right
    local placed = 0;
    xs.set1(0, spacing * 2.0);
    ys.set1(0, 0.0);
    placed = 1;
    local rank = 1;
    while (placed < n) {
        local perSide = rank;
        for (local s = 0; s < perSide && placed < n; s += 1) {
            xs.set1(placed, -rank * spacing * 0.85);
            ys.set1(placed, (s + 1) * spacing * 0.7);
            placed += 1;
            if (placed >= n) break;
            xs.set1(placed, -rank * spacing * 0.85);
            ys.set1(placed, -(s + 1) * spacing * 0.7);
            placed += 1;
        }
        rank += 1;
    }
    return stackXY1(tf, xs, ys);
}

function makeCircle(tf, n, radius) {
    // Fully tensor-built: arange → angle → cos/sin → concat
    local idx = tf.arange(n);
    local ang = tf.mulScalar(idx, TWO_PI / n.tofloat());
    local xs = tf.mulScalar(tf.cos(ang), radius);
    local ys = tf.mulScalar(tf.sin(ang), radius);
    return stackXY1(tf, xs, ys);
}

function makeLine(tf, n, spacing) {
    local idx = tf.arange(n);
    local centered = tf.subScalar(idx, (n - 1) * 0.5);
    local ys = tf.mulScalar(centered, spacing);
    local xs = tf.zeros1(n);
    return stackXY1(tf, xs, ys);
}

function makeColumn(tf, n, spacing) {
    // Two lanes trailing the leader (-X), filled from script into tensors.
    local xs = tf.zeros1(n);
    local ys = tf.zeros1(n);
    for (local i = 0; i < n; i += 1) {
        local lane = (i % 2 == 0) ? -0.55 : 0.55;
        ys.set1(i, lane * spacing);
        xs.set1(i, -((i / 2).tointeger()).tofloat() * spacing);
    }
    return stackXY1(tf, xs, ys);
}

function formationName(id) {
    if (id == 0) return "grid";
    if (id == 1) return "wedge";
    if (id == 2) return "circle";
    if (id == 3) return "line";
    return "column";
}

function buildFormation(tf, id) {
    if (id == 0) return makeGrid(tf, UNIT_COUNT, SLOT_SPACING);
    if (id == 1) return makeWedge(tf, UNIT_COUNT, SLOT_SPACING);
    if (id == 2) return makeCircle(tf, UNIT_COUNT, SLOT_SPACING * 3.2);
    if (id == 3) return makeLine(tf, UNIT_COUNT, SLOT_SPACING * 0.85);
    return makeColumn(tf, UNIT_COUNT, SLOT_SPACING);
}

function makeTransform(tf, x, y, heading) {
    // [3,2] rows: [cos,sin], [-sin,cos], [tx,ty]
    // world = offsets_homog @ M
    local c = cos(heading);
    local s = sin(heading);
    local m = tf.zeros2(3, 2);
    m.set2(0, 0, c);  m.set2(0, 1, s);
    m.set2(1, 0, -s); m.set2(1, 1, c);
    m.set2(2, 0, x);  m.set2(2, 1, y);
    return m;
}

function compileStep(tf) {
    // Compiled once: soft pursuit toward matmul-deployed slots.
    //   target = offsets[N,3] @ transform[3,2]
    //   pos   += (target - pos) * gain
    local fn = tf.func();
    local pos = fn.input2(UNIT_COUNT, 2);
    local xf = fn.input2(3, 2);
    local off = fn.input2(UNIT_COUNT, 3);
    local target = tf.matmul(off, xf);
    local delta = tf.sub(target, pos);
    local next = tf.add(pos, tf.mulScalar(delta, PURSUIT_GAIN));
    fn.setOutput(next);
    local compiled = fn.compile();
    print("[gpu-formation] compiled step device = " + compiled.getDevice() + "\n");
    return compiled;
}

function snapToSlots() {
    local xf = makeTransform(tf, leaderX, leaderY, heading);
    positions = tf.matmul(offsets, xf);
}

function switchFormation(id) {
    if (id < 0) id = 0;
    if (id > 4) id = 4;
    if (id == formation && morphT >= 1.0) return;
    offsetsFrom = offsets;
    offsetsTo = buildFormation(tf, id);
    formation = id;
    morphT = 0.0;
    print("[gpu-formation] morph -> " + formationName(id) + "\n");
}

function blendedOffsets() {
    if (morphT >= 1.0) return offsetsTo;
    local a = tf.mulScalar(offsetsFrom, 1.0 - morphT);
    local b = tf.mulScalar(offsetsTo, morphT);
    return tf.add(a, b);
}

function keyHeld(name) {
    if (!has_module("keyboard")) return false;
    return keyboard.isDown(name);
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.07, 0.10, 1.0);
    if (tf == null) tf = eve.TF();
    tf.setRandomSeed(7);

    offsetsTo = buildFormation(tf, formation);
    offsetsFrom = offsetsTo;
    offsets = offsetsTo;
    morphT = 1.0;

    stepFn = compileStep(tf);
    deviceName = stepFn.getDevice();

    // Seed units near the leader with a little noise so pursuit is visible.
    local noise = tf.mulScalar(tf.randomNormal2(UNIT_COUNT, 2), 40.0);
    local seedXf = makeTransform(tf, leaderX, leaderY, heading);
    positions = tf.add(tf.matmul(offsets, seedXf), noise);

    print("[gpu-formation] " + UNIT_COUNT + " units | formation=" +
          formationName(formation) + " | device=" + deviceName + "\n");
    print("[gpu-formation] 1-5 formation | WASD move | Q/E turn | Space pause | R reset\n");
}

eve_update = function(dt) {
    frame += 1;

    if (key_just_pressed("1")) { autoDemo = false; switchFormation(0); }
    if (key_just_pressed("2")) { autoDemo = false; switchFormation(1); }
    if (key_just_pressed("3")) { autoDemo = false; switchFormation(2); }
    if (key_just_pressed("4")) { autoDemo = false; switchFormation(3); }
    if (key_just_pressed("5")) { autoDemo = false; switchFormation(4); }
    if (key_just_pressed("space") || key_just_pressed("Space")) {
        paused = !paused;
        autoDemo = false;
    }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        offsets = blendedOffsets();
        snapToSlots();
        autoDemo = false;
    }

    if (paused) return;

    // Auto demo: slow orbit + cycle formations so smoke / first glance looks alive.
    if (autoDemo) {
        heading += 0.35 * dt;
        leaderX = 480.0 + cos(frame * 0.01) * 90.0;
        leaderY = 320.0 + sin(frame * 0.013) * 60.0;
        if (frame > 0 && frame % 180 == 0)
            switchFormation((formation + 1) % 5);
    } else {
        local mx = 0.0;
        local my = 0.0;
        if (keyHeld("a") || keyHeld("A") || keyHeld("Left")) mx -= 1.0;
        if (keyHeld("d") || keyHeld("D") || keyHeld("Right")) mx += 1.0;
        if (keyHeld("w") || keyHeld("W") || keyHeld("Up")) my -= 1.0;
        if (keyHeld("s") || keyHeld("S") || keyHeld("Down")) my += 1.0;
        if (keyHeld("q") || keyHeld("Q")) heading -= TURN_SPEED * dt;
        if (keyHeld("e") || keyHeld("E")) heading += TURN_SPEED * dt;
        leaderX += mx * LEADER_SPEED * dt;
        leaderY += my * LEADER_SPEED * dt;
        if (leaderX < 40.0) leaderX = 40.0;
        if (leaderX > 920.0) leaderX = 920.0;
        if (leaderY < 40.0) leaderY = 40.0;
        if (leaderY > 600.0) leaderY = 600.0;
    }

    if (morphT < 1.0) {
        morphT += MORPH_SPEED * dt;
        if (morphT > 1.0) morphT = 1.0;
    }
    offsets = blendedOffsets();

    local xf = makeTransform(tf, leaderX, leaderY, heading);
    // One compiled dispatch: matmul deploy + soft pursuit (GPU when available).
    positions = stepFn.run3(positions, xf, offsets);

    if (frame % 120 == 0) {
        local drift = tf.sub(positions, tf.matmul(offsets, xf));
        local meanAbs = tf.reduceMean(tf.abs(drift));
        print("[gpu-formation] frame=" + frame +
              " form=" + formationName(formation) +
              " morph=" + morphT +
              " meanSlotError=" + meanAbs +
              " device=" + deviceName + "\n");
    }
}

eve_render = function() {
    gfx.clear();

    // Ground grid (subtle).
    for (local gx = 0; gx < 960; gx += 40)
        gfx.drawSolidRect(gx.tofloat(), 0.0, 1.0, 640.0, 0.10, 0.12, 0.16, 1.0);
    for (local gy = 0; gy < 640; gy += 40)
        gfx.drawSolidRect(0.0, gy.tofloat(), 960.0, 1.0, 0.10, 0.12, 0.16, 1.0);

    // Ghost slots (deployed matmul targets) — faint rings via small rects.
    local xf = makeTransform(tf, leaderX, leaderY, heading);
    local slots = tf.matmul(offsets, xf);
    for (local i = 0; i < UNIT_COUNT; i += 1) {
        local sx = slots.get2(i, 0);
        local sy = slots.get2(i, 1);
        gfx.drawSolidRect(sx - 2.0, sy - 2.0, 4.0, 4.0, 0.25, 0.45, 0.55, 0.35);
    }

    // Units.
    for (local i = 0; i < UNIT_COUNT; i += 1) {
        local x = positions.get2(i, 0);
        local y = positions.get2(i, 1);
        local t = i.tofloat() / UNIT_COUNT.tofloat();
        local r = 0.35 + 0.55 * t;
        local g = 0.75 - 0.35 * t;
        local b = 0.95 - 0.20 * t;
        gfx.drawSolidRect(x - 4.0, y - 4.0, 8.0, 8.0, r, g, b, 0.95);
    }

    // Leader + facing tick.
    gfx.drawSolidRect(leaderX - 7.0, leaderY - 7.0, 14.0, 14.0, 1.0, 0.85, 0.25, 1.0);
    local fx = leaderX + cos(heading) * 18.0;
    local fy = leaderY + sin(heading) * 18.0;
    gfx.drawSolidRect(fx - 3.0, fy - 3.0, 6.0, 6.0, 1.0, 0.55, 0.15, 1.0);

    // HUD strips (no font dependency): color bars encode formation id + device.
    local barY = 8.0;
    for (local i = 0; i < 5; i += 1) {
        local active = (i == formation) ? 1.0 : 0.35;
        gfx.drawSolidRect(12.0 + i * 22.0, barY, 18.0, 10.0,
                          0.2 + 0.15 * i, 0.55 * active, 0.9 * active, active);
    }
    // Device badge: green = gpu, blue = cpu
    if (deviceName == "gpu")
        gfx.drawSolidRect(920.0, 8.0, 28.0, 10.0, 0.2, 0.85, 0.35, 1.0);
    else
        gfx.drawSolidRect(920.0, 8.0, 28.0, 10.0, 0.25, 0.45, 0.95, 1.0);

    // Morph progress bar.
    gfx.drawSolidRect(12.0, 24.0, 200.0, 4.0, 0.15, 0.18, 0.22, 1.0);
    gfx.drawSolidRect(12.0, 24.0, 200.0 * morphT, 4.0, 0.95, 0.7, 0.2, 1.0);
}
