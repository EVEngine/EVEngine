// Joint-tree posing for the SCAD music box (metres / Y-up / radians).
// Mirrors scad-watermill: one Renderable3D batch per named part.

dofile("ratios.nut");

local PI = 3.14159265358979;

local function rotation(y, p, r) {
    local cy = cos(y), sy = sin(y), cp = cos(p), sp = sin(p), cr = cos(r), sr = sin(r);
    return [cy * cr + sy * sp * sr, -cy * sr + sy * sp * cr, sy * cp,
        cp * sr, cp * cr, -sp, -sy * cr + cy * sp * sr, sy * sr + cy * sp * cr, cy * cp];
}

local function vector(m, v) {
    return [m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
        m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
        m[6] * v[0] + m[7] * v[1] + m[8] * v[2]];
}

local function multiply(a, b) {
    local m = [];
    for (local row = 0; row < 3; row++)
        for (local col = 0; col < 3; col++)
            m.push(a[row * 3] * b[col] + a[row * 3 + 1] * b[col + 3] + a[row * 3 + 2] * b[col + 6]);
    return m;
}

local function axisAngle(axis, angle) {
    local len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (len < 1e-8) throw "zero joint axis";
    local x = axis[0] / len, y = axis[1] / len, z = axis[2] / len;
    local c = cos(angle), s = sin(angle), t = 1.0 - c;
    return [
        t * x * x + c, t * x * y - s * z, t * x * z + s * y,
        t * x * y + s * z, t * y * y + c, t * y * z - s * x,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c
    ];
}

local function clamp(value, lo, hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

function boxLoadRig(path) {
    // Compact EveScript table mirroring eve.scad-rig/1 (assets/musicbox.rig.json).
    // Pivots are already metres / Y-up and parent-relative.
    local rig = {
        nodes = {
            shell = {parent = null, p = [0.0, 0.0, 0.0], axis = [0.0, 1.0, 0.0], limits = [0.0, 0.0]},
            lid = {parent = "shell", p = [0.0, 0.058, -0.055], axis = [1.0, 0.0, 0.0], limits = [-95.0, 0.0]},
            bed = {parent = "shell", p = [0.0, 0.018, 0.0], axis = [0.0, 1.0, 0.0], limits = [0.0, 0.0]},
            barrel = {parent = "bed", p = [-0.058, 0.022, -0.008], axis = [0.0, 1.0, 0.0], limits = [-1e6, 1e6]},
            cylinder = {parent = "bed", p = [0.008, 0.028, 0.006], axis = [1.0, 0.0, 0.0], limits = [-1e6, 1e6]},
            comb = {parent = "bed", p = [0.008, 0.022, -0.022], axis = [0.0, 1.0, 0.0], limits = [0.0, 0.0]},
            governor = {parent = "bed", p = [0.062, 0.034, -0.010], axis = [0.0, 1.0, 0.0], limits = [-1e6, 1e6]},
            dancer = {parent = "shell", p = [0.0, 0.062, 0.028], axis = [0.0, 1.0, 0.0], limits = [-1e6, 1e6]}
        },
        order = ["shell", "lid", "bed", "barrel", "cylinder", "comb", "governor", "dancer"],
        path = path
    };
    return rig;
}

function boxSyncGears(instance) {
    local barrel = instance.angles.barrel;
    instance.angles.cylinder = barrel * boxBarrelToCylinder;
    instance.angles.governor = barrel * boxBarrelToGovernor;
    instance.angles.dancer = instance.angles.cylinder * boxCylinderToDancer;
}

function boxPose(instance) {
    local transforms = {};
    local root = rotation(instance.yaw, 0.0, 0.0);
    foreach (name in instance.rig.order) {
        local node = instance.rig.nodes[name];
        local degrees = (name in instance.angles) ? instance.angles[name] : 0.0;
        degrees = clamp(degrees, node.limits[0], node.limits[1]);
        instance.angles[name] = degrees;
        local localRot = axisAngle(node.axis, degrees * PI / 180.0);
        local pos = [node.p[0], node.p[1], node.p[2]];
        local rot = localRot;
        if (node.parent != null) {
            local parent = transforms[node.parent];
            local world = vector(parent.r, pos);
            pos = [parent.p[0] + world[0], parent.p[1] + world[1], parent.p[2] + world[2]];
            rot = multiply(parent.r, localRot);
        } else {
            local shifted = vector(root, pos);
            pos = [instance.x + shifted[0], instance.y + shifted[1], instance.z + shifted[2]];
            rot = multiply(root, localRot);
        }
        transforms[name] <- {r = rot, p = pos};
    }
    instance.transforms <- transforms;
    foreach (part in instance.parts) {
        local t = transforms[part.j];
        local r = t.r, p = t.p;
        local v = r[5] < -1.0 ? -1.0 : (r[5] > 1.0 ? 1.0 : r[5]);
        local pitch = asin(-v), yaw = atan2(r[2], r[8]), roll = atan2(r[3], r[4]);
        part.e.setPosition(p[0].tofloat(), p[1].tofloat(), p[2].tofloat());
        part.e.setRotation(yaw.tofloat(), pitch.tofloat(), roll.tofloat());
    }
}
