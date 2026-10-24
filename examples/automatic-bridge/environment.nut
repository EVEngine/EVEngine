// Shared procedural presentation: shallow river, gravel banks and small riprap.
// Geometry comes from deterministic coordinate functions, without external models.
persist bridgeEnvironment = []
persist bridgeEnvironmentCache = {}

function bridgeEnvironmentPart(mesh, material, candidate) {
    local part = eve.Renderable3D();
    part.setVisible(false);
    part.setMesh(mesh);
    part.setMaterial(bridgeMaterial(material));
    part.setCamera(bridgeCamera);
    part.setReceiveShadow(true);
    part.setCastShadow(material != "water");
    candidate.append(part);
    return part;
}

function bridgeBankHeight(x, z, span, deckHeight) {
    local bank = (abs(x) - span * 0.28) / (span * 0.2);
    bank = bank < 0.0 ? 0.0 : (bank > 1.0 ? 1.0 : bank);
    bank = bank * bank * (3.0 - 2.0 * bank);
    local ripple = 0.08 * sin(x * 2.1 + z * 0.8) * sin(z * 1.8 - x * 0.7);
    return -0.55 + bank * (deckHeight - 0.18) + ripple;
}

function bridgeBuildEnvironment(span, deckHeight) {
    local key = span + ":" + deckHeight;
    if (key in bridgeEnvironmentCache) {
        foreach (old in bridgeEnvironment) old.setVisible(false);
        bridgeEnvironment = bridgeEnvironmentCache[key].parts;
        foreach (part in bridgeEnvironment) part.setVisible(true);
        return;
    }
    local candidate = [];
    local pos = [], normals = [], uv = [], indices = [];
    local nx = 100, nz = 40, stride = nx + 1;
    local extentX = span * 0.83, extentZ = span * 0.34;
    for (local z = 0; z <= nz; ++z) for (local x = 0; x <= nx; ++x) {
        local wx = -extentX + 2.0 * extentX * x / nx;
        local wz = -extentZ + 2.0 * extentZ * z / nz;
        local h = bridgeBankHeight(wx, wz, span, deckHeight);
        local eps = 0.08;
        local dx = (bridgeBankHeight(wx + eps, wz, span, deckHeight) - bridgeBankHeight(wx - eps, wz, span, deckHeight)) / (2.0 * eps);
        local dz = (bridgeBankHeight(wx, wz + eps, span, deckHeight) - bridgeBankHeight(wx, wz - eps, span, deckHeight)) / (2.0 * eps);
        local len = sqrt(1.0 + dx * dx + dz * dz);
        pos.extend([wx, h, wz]); normals.extend([-dx / len, 1.0 / len, -dz / len]); uv.extend([wx * 0.4, wz * 0.4]);
        if (x < nx && z < nz) {
            local i = z * stride + x;
            indices.extend([i, i + stride, i + 1, i + 1, i + stride, i + stride + 1]);
        }
    }
    bridgeEnvironmentPart(gfx.newMeshFromArrays(pos, normals, uv, pos.len() / 3, indices, indices.len()), "gravel", candidate);
    // River participates in the same material/render pass as the bridge.
    // A rippled PBR surface keeps scene composition independent of postprocess order.
    local wp = [], wn = [], wt = [], wi = [];
    for (local z = 0; z <= 40; ++z) for (local x = 0; x <= 60; ++x) {
        local wx = span * 0.94 * (x / 60.0 - 0.5);
        local wz = extentZ * 2.1 * (z / 40.0 - 0.5);
        wp.extend([wx, -0.18 + 0.018 * sin(wx * 2.0 + wz * 3.0), wz]);
        wn.extend([0.0, 1.0, 0.0]); wt.extend([wx * 0.22, wz * 0.22]);
        if (x < 60 && z < 40) {
            local i = z * 61 + x;
            wi.extend([i, i + 61, i + 1, i + 1, i + 61, i + 62]);
        }
    }
    bridgeEnvironmentPart(gfx.newMeshFromArrays(wp, wn, wt, wp.len() / 3, wi, wi.len()), "water", candidate);
    // One combined pebble mesh avoids a draw call and entity for every stone.
    local rockPos = [], rockNormals = [], rockUv = [], rockIndices = [];
    local phi = 1.618034;
    local stoneVertices = [[-1.0,phi,0.0],[1.0,phi,0.0],[-1.0,-phi,0.0],[1.0,-phi,0.0],
                  [0.0,-1.0,phi],[0.0,1.0,phi],[0.0,-1.0,-phi],[0.0,1.0,-phi],
                  [phi,0.0,-1.0],[phi,0.0,1.0],[-phi,0.0,-1.0],[-phi,0.0,1.0]];
    local faces = [0,11,5,0,5,1,0,1,7,0,7,10,0,10,11,1,5,9,5,11,4,11,10,2,10,7,6,7,1,8,
                   3,9,4,3,4,2,3,2,6,3,6,8,3,8,9,4,9,5,2,4,11,6,2,10,8,6,7,9,8,1];
    for (local i = 0; i < 1600; ++i) {
        local n = i.tofloat() + 37.0;
        local hx = sin(n * 12.9898) * 43758.5453; hx -= floor(hx);
        local hz = sin(n * 73.156 + 31.0) * 19341.339; hz -= floor(hz);
        local hs = sin(n * 23.117 + 81.0) * 33711.781; hs -= floor(hs);
        local sign = i % 2 == 0 ? -1.0 : 1.0;
        local x = sign * span * (0.30 + 0.28 * hx), z = extentZ * 1.92 * (hz - 0.5);
        local y = bridgeBankHeight(x, z, span, deckHeight);
        local radius = 0.07 + 0.17 * hs;
        local first = rockPos.len() / 3;
        foreach (v in stoneVertices) {
            local len = sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
            local vx = v[0] / len, vy = v[1] / len, vz = v[2] / len;
            rockPos.extend([x + vx * radius * 1.25, y + radius * 0.25 + vy * radius * 0.65, z + vz * radius]);
            rockNormals.extend([vx, vy, vz]); rockUv.extend([vx + hx, vz + hz]);
        }
        foreach (f in faces) rockIndices.append(first + f);
    }
    bridgeEnvironmentPart(gfx.newMeshFromArrays(rockPos, rockNormals, rockUv, rockPos.len() / 3, rockIndices, rockIndices.len()), "gravel", candidate);
    foreach (old in bridgeEnvironment) old.setVisible(false);
    bridgeEnvironmentCache[key] <- { parts = candidate };
    bridgeEnvironment = candidate;
    foreach (part in bridgeEnvironment) part.setVisible(true);
}
