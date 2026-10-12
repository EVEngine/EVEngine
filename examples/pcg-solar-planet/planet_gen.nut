// Sphere topologies + CPU height field for examples/pcg-solar-planet.
// Mirrors SebLague Solar-System Episode_02 ideas using eve.Math / gfx mesh arrays.

const PI = 3.141592653589793;
const TAU = 6.283185307179586;

function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function fmax(a, b) { return a > b ? a : b; }
function fmin(a, b) { return a < b ? a : b; }

function lerp(a, b, t) { return a + (b - a) * t; }

function length3(x, y, z) { return sqrt(x * x + y * y + z * z); }

function normalize3(x, y, z) {
    local len = length3(x, y, z);
    if (len < 1e-8) return [0.0, 1.0, 0.0];
    return [x / len, y / len, z / len];
}

// iq / SebLague polynomial smooth-min (tutorial crater blending).
function smoothMin(a, b, k) {
    k = fmax(0.0, k);
    if (k < 1e-8) return fmin(a, b);
    local h = clampf((b - a + k) / (2.0 * k), 0.0, 1.0);
    return a * h + b * (1.0 - h) - k * h * (1.0 - h);
}

// Match SebLague Math.cginc smoothMax used by crater rims (k-style, not width-band).
function smoothMaxK(a, b, k) {
    return -smoothMin(-a, -b, k);
}

function emptyMesh() {
    return { pos = [], nrm = [], uv = [], idx = [], dirs = [], heights = [], minHeight = 1.0, maxHeight = 1.0 };
}

function pushVert(mesh, x, y, z, u, v) {
    local n = normalize3(x, y, z);
    mesh.pos.push(n[0]); mesh.pos.push(n[1]); mesh.pos.push(n[2]);
    mesh.nrm.push(n[0]); mesh.nrm.push(n[1]); mesh.nrm.push(n[2]);
    mesh.uv.push(u); mesh.uv.push(v);
    mesh.dirs.push(n);
    return mesh.dirs.len() - 1;
}

function pushTri(mesh, a, b, c) {
    mesh.idx.push(a); mesh.idx.push(b); mesh.idx.push(c);
}

// --- topologies -------------------------------------------------------------

function buildUvSphere(slices, stacks) {
    local mesh = emptyMesh();
    for (local i = 0; i <= stacks; ++i) {
        local v = i.tofloat() / stacks.tofloat();
        local phi = v * PI;
        local sp = sin(phi);
        local cp = cos(phi);
        for (local j = 0; j <= slices; ++j) {
            local u = j.tofloat() / slices.tofloat();
            local theta = u * TAU;
            pushVert(mesh, sp * cos(theta), cp, sp * sin(theta), u, v);
        }
    }
    local stride = slices + 1;
    for (local i = 0; i < stacks; ++i) {
        for (local j = 0; j < slices; ++j) {
            local a = i * stride + j;
            local b = a + 1;
            local c = (i + 1) * stride + j;
            local d = c + 1;
            if (i != 0) { pushTri(mesh, a, c, b); }
            if (i != stacks - 1) { pushTri(mesh, b, c, d); }
        }
    }
    return mesh;
}

function buildCubeSphere(resolution) {
    local mesh = emptyMesh();
    local faces = [
        [ 1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0],
        [-1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0,-1.0],
        [ 0.0, 1.0, 0.0,  0.0, 0.0, 1.0,  1.0, 0.0, 0.0],
        [ 0.0,-1.0, 0.0,  0.0, 0.0,-1.0,  1.0, 0.0, 0.0],
        [ 0.0, 0.0, 1.0,  0.0, 1.0, 0.0, -1.0, 0.0, 0.0],
        [ 0.0, 0.0,-1.0,  0.0, 1.0, 0.0,  1.0, 0.0, 0.0]
    ];
    local res = fmax(2, resolution);
    foreach (face in faces) {
        local faceOrigin = mesh.dirs.len();
        for (local y = 0; y <= res; ++y) {
            local v = y.tofloat() / res.tofloat();
            local fy = lerp(-1.0, 1.0, v);
            for (local x = 0; x <= res; ++x) {
                local u = x.tofloat() / res.tofloat();
                local fx = lerp(-1.0, 1.0, u);
                local px = face[0] + face[3] * fy + face[6] * fx;
                local py = face[1] + face[4] * fy + face[7] * fx;
                local pz = face[2] + face[5] * fy + face[8] * fx;
                pushVert(mesh, px, py, pz, u, v);
            }
        }
        local stride = res + 1;
        for (local y = 0; y < res; ++y) {
            for (local x = 0; x < res; ++x) {
                local a = faceOrigin + y * stride + x;
                local b = a + 1;
                local c = a + stride;
                local d = c + 1;
                pushTri(mesh, a, c, b);
                pushTri(mesh, b, c, d);
            }
        }
    }
    return mesh;
}

function icosaBase() {
    local t = (1.0 + sqrt(5.0)) * 0.5;
    local verts = [
        [-1.0,  t, 0.0], [ 1.0,  t, 0.0], [-1.0, -t, 0.0], [ 1.0, -t, 0.0],
        [ 0.0,-1.0,  t], [ 0.0, 1.0,  t], [ 0.0,-1.0, -t], [ 0.0, 1.0, -t],
        [ t, 0.0,-1.0], [ t, 0.0, 1.0], [-t, 0.0,-1.0], [-t, 0.0, 1.0]
    ];
    local faces = [
        [0,11,5],[0,5,1],[0,1,7],[0,7,10],[0,10,11],
        [1,5,9],[5,11,4],[11,10,2],[10,7,6],[7,1,8],
        [3,9,4],[3,4,2],[3,2,6],[3,6,8],[3,8,9],
        [4,9,5],[2,4,11],[6,2,10],[8,6,7],[9,8,1]
    ];
    return { verts = verts, faces = faces };
}

function midpointKey(a, b) {
    if (a > b) { local t = a; a = b; b = t; }
    return a.tostring() + ":" + b.tostring();
}

function buildIcosphere(subdivisions) {
    local seed = icosaBase();
    local verts = [];
    foreach (v in seed.verts) {
        local n = normalize3(v[0], v[1], v[2]);
        verts.push(n);
    }
    local faces = seed.faces;
    local levels = fmax(0, subdivisions);
    for (local level = 0; level < levels; ++level) {
        local midCache = {};
        local nextFaces = [];
        foreach (f in faces) {
            local mids = [];
            for (local e = 0; e < 3; ++e) {
                local i0 = f[e];
                local i1 = f[(e + 1) % 3];
                local key = midpointKey(i0, i1);
                if (!(key in midCache)) {
                    local a = verts[i0], b = verts[i1];
                    local n = normalize3(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
                    midCache[key] <- verts.len();
                    verts.push(n);
                }
                mids.push(midCache[key]);
            }
            nextFaces.push([f[0], mids[0], mids[2]]);
            nextFaces.push([f[1], mids[1], mids[0]]);
            nextFaces.push([f[2], mids[2], mids[1]]);
            nextFaces.push([mids[0], mids[1], mids[2]]);
        }
        faces = nextFaces;
    }

    local mesh = emptyMesh();
    for (local i = 0; i < verts.len(); ++i) {
        local v = verts[i];
        local u = 0.5 + atan2(v[2], v[0]) / TAU;
        local vv = 0.5 - asin(clampf(v[1], -1.0, 1.0)) / PI;
        pushVert(mesh, v[0], v[1], v[2], u, vv);
    }
    foreach (f in faces) pushTri(mesh, f[0], f[1], f[2]);
    return mesh;
}

function buildFibonacciSphere(count) {
    local n = fmax(32, count);
    local mesh = emptyMesh();
    local golden = PI * (3.0 - sqrt(5.0));
    local points = [];
    for (local i = 0; i < n; ++i) {
        local y = 1.0 - (i.tofloat() / (n - 1).tofloat()) * 2.0;
        local radius = sqrt(fmax(0.0, 1.0 - y * y));
        local theta = golden * i.tofloat();
        local x = cos(theta) * radius;
        local z = sin(theta) * radius;
        local u = theta / TAU;
        local v = 0.5 - y * 0.5;
        local id = pushVert(mesh, x, y, z, u, v);
        points.push({ i = id, x = x, y = y, z = z, theta = atan2(z, x) });
    }

    // Band stitch: sort into latitude rings, then connect consecutive rings.
    local bands = 18;
    local buckets = [];
    for (local b = 0; b < bands; ++b) buckets.push([]);
    foreach (p in points) {
        local bi = clampf(((1.0 - p.y) * 0.5 * bands).tointeger(), 0, bands - 1);
        buckets[bi].push(p);
    }
    for (local b = 0; b < bands; ++b) {
        local band = buckets[b];
        for (local i = 0; i < band.len(); ++i) {
            for (local j = i + 1; j < band.len(); ++j) {
                if (band[i].theta > band[j].theta) {
                    local tmp = band[i]; band[i] = band[j]; band[j] = tmp;
                }
            }
        }
    }
    for (local b = 0; b < bands - 1; ++b) {
        local a = buckets[b];
        local c = buckets[b + 1];
        if (a.len() == 0 || c.len() == 0) continue;
        local i = 0, j = 0;
        while (i < a.len() - 1 || j < c.len() - 1) {
            local a0 = a[fmin(i, a.len() - 1)].i;
            local a1 = a[fmin(i + 1, a.len() - 1)].i;
            local c0 = c[fmin(j, c.len() - 1)].i;
            local c1 = c[fmin(j + 1, c.len() - 1)].i;
            if (i >= a.len() - 1) {
                pushTri(mesh, a0, c0, c1);
                j += 1;
            } else if (j >= c.len() - 1) {
                pushTri(mesh, a0, c0, a1);
                i += 1;
            } else if (a[i].theta <= c[j].theta) {
                pushTri(mesh, a0, c0, a1);
                i += 1;
            } else {
                pushTri(mesh, a0, c0, c1);
                j += 1;
            }
        }
    }
    return mesh;
}

function buildTopology(name, resolution) {
    // Slider 1..5 maps to comparable visual density across topologies.
    local level = fmax(1, resolution);
    if (name == "uv") return buildUvSphere(12 + level * 12, 8 + level * 6);
    if (name == "cube") return buildCubeSphere(4 + level * 3);
    if (name == "ico") return buildIcosphere(fmin(4, level)); // subdiv 4 ≈ 5120 tris
    if (name == "fibonacci") return buildFibonacciSphere(120 + level * 100);
    return buildIcosphere(fmin(4, level));
}

// --- height field -----------------------------------------------------------

function makeCraters(seed, count, sizeMin, sizeMax) {
    math.setRandomSeed(seed);
    local craters = [];
    for (local i = 0; i < count; ++i) {
        local t = math.random();
        t = t * t; // bias toward smaller
        local size = lerp(sizeMin, sizeMax, t);
        local floorH = lerp(-1.1, -0.25, t + math.random() * 0.2);
        local smooth = lerp(0.35, 1.4, 1.0 - t);
        // Random unit direction.
        local z = math.random() * 2.0 - 1.0;
        local a = math.random() * TAU;
        local r = sqrt(fmax(0.0, 1.0 - z * z));
        craters.push({
            cx = cos(a) * r, cy = z, cz = sin(a) * r,
            size = size, floor = floorH, smooth = smooth
        });
    }
    return craters;
}

function craterDepth(dir, craters, rimSteepness, rimWidth) {
    local h = 0.0;
    foreach (c in craters) {
        local dx = dir[0] - c.cx;
        local dy = dir[1] - c.cy;
        local dz = dir[2] - c.cz;
        local x = length3(dx, dy, dz) / fmax(c.size, 1e-4);
        local cavity = x * x - 1.0;
        local rimX = fmin(x - 1.0 - rimWidth, 0.0);
        local rim = rimSteepness * rimX * rimX;
        local shape = smoothMaxK(cavity, c.floor, c.smooth);
        shape = smoothMin(shape, rim, c.smooth);
        h += shape * c.size;
    }
    return h;
}

function sampleHeight(dir, cfg, craters) {
    local sx = dir[0] * cfg.noiseScale + cfg.seedOffset;
    local sy = dir[1] * cfg.noiseScale + cfg.seedOffset * 1.37;
    local sz = dir[2] * cfg.noiseScale + cfg.seedOffset * 2.11;

    // Domain warp (Episode 02 "warped noise").
    if (cfg.warp > 0.0) {
        local wx = math.noise3(sx + 17.1, sy - 3.4, sz + 9.2) * cfg.warp;
        local wy = math.noise3(sx - 8.6, sy + 21.3, sz - 4.7) * cfg.warp;
        local wz = math.noise3(sx + 5.5, sy - 12.8, sz + 33.1) * cfg.warp;
        sx += wx; sy += wy; sz += wz;
    }

    local continent = math.fbm3(sx, sy, sz, cfg.octaves, 2.0, 0.5);
    continent = continent * 2.0 - 1.0; // roughly [-1,1]
    continent = math.smoothMax(continent, -cfg.oceanFloorDepth, cfg.oceanFloorSmoothing);
    if (continent < 0.0) continent *= (1.0 + cfg.oceanDepthMultiplier);

    local ridge = 0.0;
    if (cfg.ridgeStrength > 0.0) {
        ridge = math.ridged3(sx * cfg.ridgeScale, sy * cfg.ridgeScale, sz * cfg.ridgeScale,
                             cfg.ridgeOctaves, 2.1, 0.55);
        ridge = ridge * cfg.ridgeStrength;
    }

    local mask = 1.0;
    if (cfg.mountainBlend > 0.0) {
        local maskNoise = math.fbm3(sx * 0.7 + 40.0, sy * 0.7, sz * 0.7 - 12.0, 3, 2.0, 0.5);
        mask = clampf((maskNoise - (1.0 - cfg.mountainBlend)) / fmax(cfg.mountainBlend, 1e-3), 0.0, 1.0);
    }

    local cratersH = 0.0;
    if (craters != null && craters.len() > 0)
        cratersH = craterDepth(dir, craters, cfg.rimSteepness, cfg.rimWidth) * cfg.craterStrength;

    return 1.0 + continent * cfg.elevation + ridge * cfg.elevation * mask + cratersH;
}

function recomputeNormals(mesh) {
    local vc = mesh.dirs.len();
    local acc = [];
    for (local i = 0; i < vc; ++i) acc.push([0.0, 0.0, 0.0]);
    local idx = mesh.idx;
    for (local t = 0; t + 2 < idx.len(); t += 3) {
        local i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        local ax = mesh.pos[i0 * 3], ay = mesh.pos[i0 * 3 + 1], az = mesh.pos[i0 * 3 + 2];
        local bx = mesh.pos[i1 * 3], by = mesh.pos[i1 * 3 + 1], bz = mesh.pos[i1 * 3 + 2];
        local cx = mesh.pos[i2 * 3], cy = mesh.pos[i2 * 3 + 1], cz = mesh.pos[i2 * 3 + 2];
        local ux = bx - ax, uy = by - ay, uz = bz - az;
        local vx = cx - ax, vy = cy - ay, vz = cz - az;
        local nx = uy * vz - uz * vy;
        local ny = uz * vx - ux * vz;
        local nz = ux * vy - uy * vx;
        acc[i0][0] += nx; acc[i0][1] += ny; acc[i0][2] += nz;
        acc[i1][0] += nx; acc[i1][1] += ny; acc[i1][2] += nz;
        acc[i2][0] += nx; acc[i2][1] += ny; acc[i2][2] += nz;
    }
    for (local i = 0; i < vc; ++i) {
        local n = normalize3(acc[i][0], acc[i][1], acc[i][2]);
        // Flip inward faces (rare on fibonacci stitch).
        local d = mesh.dirs[i];
        if (n[0] * d[0] + n[1] * d[1] + n[2] * d[2] < 0.0) {
            n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
        }
        mesh.nrm[i * 3] = n[0];
        mesh.nrm[i * 3 + 1] = n[1];
        mesh.nrm[i * 3 + 2] = n[2];
    }
}

function applyHeightField(mesh, cfg) {
    local craters = null;
    if (cfg.craterCount > 0)
        craters = makeCraters(cfg.seed, cfg.craterCount, cfg.craterSizeMin, cfg.craterSizeMax);

    local minH = 1e9, maxH = -1e9;
    local heights = [];
    for (local i = 0; i < mesh.dirs.len(); ++i) {
        local h = sampleHeight(mesh.dirs[i], cfg, craters);
        heights.push(h);
        if (h < minH) minH = h;
        if (h > maxH) maxH = h;
    }
    if (maxH - minH < 1e-5) { minH = 0.99; maxH = 1.01; }

    for (local i = 0; i < mesh.dirs.len(); ++i) {
        local d = mesh.dirs[i];
        local h = heights[i];
        mesh.pos[i * 3] = d[0] * h;
        mesh.pos[i * 3 + 1] = d[1] * h;
        mesh.pos[i * 3 + 2] = d[2] * h;
        local h01 = (h - minH) / (maxH - minH);
        mesh.uv[i * 2] = h01;
        // Biome hash from direction (stable, UV-free).
        mesh.uv[i * 2 + 1] = math.hash3(d[0] * 7.1 + cfg.seedOffset,
                                        d[1] * 3.3, d[2] * 5.9);
    }
    mesh.heights <- heights;
    mesh.minHeight <- minH;
    mesh.maxHeight <- maxH;
    recomputeNormals(mesh);
    return mesh;
}

function heightConfigForPreset(preset, seed) {
    local cfg = {
        seed = seed,
        seedOffset = (seed % 10000).tofloat() * 0.017,
        noiseScale = 1.6,
        octaves = 5,
        elevation = 0.085,
        oceanFloorDepth = 0.15,
        oceanFloorSmoothing = 0.35,
        oceanDepthMultiplier = 1.8,
        ridgeStrength = 0.55,
        ridgeScale = 2.4,
        ridgeOctaves = 4,
        mountainBlend = 0.55,
        warp = 0.0,
        craterCount = 0,
        craterSizeMin = 0.03,
        craterSizeMax = 0.14,
        craterStrength = 0.045,
        rimSteepness = 0.13,
        rimWidth = 0.55,
        oceanLevel01 = 0.42,
        showOcean = true
    };
    if (preset == "earth") {
        cfg.noiseScale = 1.55;
        cfg.elevation = 0.09;
        cfg.ridgeStrength = 0.65;
        cfg.mountainBlend = 0.5;
        cfg.warp = 0.15;
        cfg.oceanLevel01 = 0.40;
    } else if (preset == "moon") {
        cfg.noiseScale = 1.1;
        cfg.elevation = 0.03;
        cfg.ridgeStrength = 0.35;
        cfg.mountainBlend = 1.0;
        cfg.oceanFloorDepth = 0.0;
        cfg.craterCount = 90;
        cfg.craterStrength = 0.06;
        cfg.showOcean = false;
        cfg.oceanLevel01 = 0.0;
    } else if (preset == "hyperion") {
        // Irregular sponge-like body: strong warp + ridged + sparse deep craters.
        cfg.noiseScale = 2.2;
        cfg.elevation = 0.12;
        cfg.ridgeStrength = 0.95;
        cfg.ridgeScale = 3.2;
        cfg.mountainBlend = 1.0;
        cfg.warp = 0.55;
        cfg.craterCount = 40;
        cfg.craterSizeMin = 0.05;
        cfg.craterSizeMax = 0.22;
        cfg.craterStrength = 0.08;
        cfg.showOcean = false;
        cfg.oceanLevel01 = 0.0;
        cfg.oceanFloorDepth = 0.0;
    } else if (preset == "bare") {
        cfg.elevation = 0.0;
        cfg.ridgeStrength = 0.0;
        cfg.craterCount = 0;
        cfg.showOcean = false;
        cfg.oceanLevel01 = 0.0;
    }
    return cfg;
}
