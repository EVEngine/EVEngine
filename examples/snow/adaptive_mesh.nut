// SnowField remains the simulation grid. This disposable quadtree projects it
// into a variable-resolution mesh; no independent copy owns snow state.
adaptiveVertexCount <- 0;
adaptiveTriangleCount <- 0;
adaptiveLeafCount <- 0;
adaptiveMinSpan <- 0;
adaptiveMaxSpan <- 0;
adaptiveTopologyAudit <- false;

function auditAdaptiveSnowMesh(pos, indices) {
    local edges = {};
    for (local i = 0; i < indices.len(); i += 3) {
        for (local j = 0; j < 3; j++) {
            local a = indices[i + j], b = indices[i + (j + 1) % 3];
            if (a < 0 || b < 0 || a * 3 >= pos.len() || b * 3 >= pos.len() || a == b)
                throw "invalid adaptive triangle";
            local key = snowMin(a, b) + ":" + snowMax(a, b);
            if (key in edges) edges[key][0]++; else edges[key] <- [1, a, b];
        }
    }
    local extent = (W - 1) * CELL;
    foreach (edge in edges) {
        if (edge[0] == 2) continue;
        local a = edge[1] * 3, b = edge[2] * 3;
        local border = (pos[a] == 0.0 && pos[b] == 0.0) ||
                       (pos[a + 2] == 0.0 && pos[b + 2] == 0.0) ||
                       (pos[a] == extent && pos[b] == extent) ||
                       (pos[a + 2] == extent && pos[b + 2] == extent);
        if (edge[0] != 1 || !border) throw "adaptive mesh has an open seam or non-manifold edge";
    }
}

function updateAdaptiveSnowMesh() {
    local n = W - 1;
    if (W != H || n < 2 || (n & (n - 1)) != 0)
        throw "adaptive snow requires a square power-of-two cell grid";
    local heights = array(W * H, 0.0);
    local snowHeights = array(W * H, 0.0);
    for (local z = 0; z < H; z++) {
        for (local x = 0; x < W; x++) {
            heights[z * W + x] = combinedHm.height(x, z) * HSCALE;
            snowHeights[z * W + x] = sf.height(x, z);
        }
    }
    local sample = function(x, z) {
        x = clampf(x, 0.0, n.tofloat());
        z = clampf(z, 0.0, n.tofloat());
        local ix = x.tointeger(), iz = z.tointeger();
        local jx = snowMin(ix + 1, n), jz = snowMin(iz + 1, n);
        local tx = x - ix, tz = z - iz;
        return (heights[iz * W + ix] * (1.0 - tx) + heights[iz * W + jx] * tx) * (1.0 - tz) +
               (heights[jz * W + ix] * (1.0 - tx) + heights[jz * W + jx] * tx) * tz;
    };
    // Min/max pyramid detects even a footprint fully inside a coarse tile.
    local lows = [], highs = [];
    local lo = array(n * n, 0.0), hi = array(n * n, 0.0);
    for (local z = 0; z < n; z++) {
        for (local x = 0; x < n; x++) {
            local a = snowHeights[z * W + x], b = snowHeights[z * W + x + 1];
            local c = snowHeights[(z + 1) * W + x], d = snowHeights[(z + 1) * W + x + 1];
            lo[z * n + x] = snowMin(snowMin(a, b), snowMin(c, d));
            hi[z * n + x] = snowMax(snowMax(a, b), snowMax(c, d));
            // Refine a normal-support band too: otherwise a coarse triangle
            // interpolates a pit-edge normal across a large untouched area.
            foreach (offset in [[-2, 0], [3, 0], [0, -2], [0, 3]]) {
                local xx = snowMax(0, snowMin(n, x + offset[0]));
                local zz = snowMax(0, snowMin(n, z + offset[1]));
                local value = snowHeights[zz * W + xx];
                lo[z * n + x] = snowMin(lo[z * n + x], value);
                hi[z * n + x] = snowMax(hi[z * n + x], value);
            }
        }
    }
    lows.append(lo); highs.append(hi);
    for (local width = n; width > 1; width /= 2) {
        local next = width / 2;
        local nextLo = array(next * next, 0.0), nextHi = array(next * next, 0.0);
        for (local z = 0; z < next; z++) {
            for (local x = 0; x < next; x++) {
                local i = z * 2 * width + x * 2;
                nextLo[z * next + x] = snowMin(snowMin(lo[i], lo[i + 1]), snowMin(lo[i + width], lo[i + width + 1]));
                nextHi[z * next + x] = snowMax(snowMax(hi[i], hi[i + 1]), snowMax(hi[i + width], hi[i + width + 1]));
            }
        }
        lo = nextLo; hi = nextHi;
        lows.append(lo); highs.append(hi);
    }
    local leaves = [];
    local visit = null;
    visit = function(x, z, span, level) {
        local width = n / span;
        local i = (z / span) * width + x / span;
        local half = span / 2;
        local centre = sample((x + half).tofloat(), (z + half).tofloat());
        local average = (sample(x.tofloat(), z.tofloat()) + sample((x + span).tofloat(), z.tofloat()) +
                         sample(x.tofloat(), (z + span).tofloat()) +
                         sample((x + span).tofloat(), (z + span).tofloat())) * 0.25;
        if (span > 1 && (span > 16 || highs[level][i] - lows[level][i] > 0.012 ||
                         abs(centre - average) > 0.003)) {
            visit(x, z, half, level - 1); visit(x + half, z, half, level - 1);
            visit(x, z + half, half, level - 1); visit(x + half, z + half, half, level - 1);
        } else leaves.append([x, z, span]);
    };
    visit(0, 0, n, lows.len() - 1);

    // Both sides use the union of edge vertices. A centre fan stitches any
    // coarse/fine ratio without skirts, T-junctions or duplicated edge positions.
    local rows = [], columns = [];
    for (local i = 0; i < W; i++) { rows.append({}); columns.append({}); }
    foreach (leaf in leaves) {
        local x = leaf[0], z = leaf[1], s = leaf[2];
        foreach (xx in [x, x + s]) foreach (zz in [z, z + s]) {
            rows[zz][xx] <- true; columns[xx][zz] <- true;
        }
    }
    local pos = [], normals = [], uv = [], indices = [], cache = {};
    local vertex = function(x, z) {
        local key = (z * 2).tointeger() * (W * 2) + (x * 2).tointeger();
        if (key in cache) return cache[key];
        local index = pos.len() / 3;
        cache[key] <- index;
        pos.extend([x * CELL, sample(x, z), z * CELL]);
        local nx = -(sample(x + 1.0, z) - sample(x - 1.0, z)) / (2.0 * CELL);
        local nz = -(sample(x, z + 1.0) - sample(x, z - 1.0)) / (2.0 * CELL);
        local length = sqrt(nx * nx + 1.0 + nz * nz);
        normals.extend([nx / length, 1.0 / length, nz / length]);
        uv.extend([x / n.tofloat(), z / n.tofloat()]);
        return index;
    };
    adaptiveMinSpan = n; adaptiveMaxSpan = 0;
    foreach (leaf in leaves) {
        local x = leaf[0], z = leaf[1], s = leaf[2];
        adaptiveMinSpan = snowMin(adaptiveMinSpan, s); adaptiveMaxSpan = snowMax(adaptiveMaxSpan, s);
        local ring = [];
        for (local zz = z; zz < z + s; zz++)
            if (zz in columns[x]) ring.append(vertex(x.tofloat(), zz.tofloat()));
        for (local xx = x; xx < x + s; xx++)
            if (xx in rows[z + s]) ring.append(vertex(xx.tofloat(), (z + s).tofloat()));
        for (local zz = z + s; zz > z; zz--)
            if (zz in columns[x + s]) ring.append(vertex((x + s).tofloat(), zz.tofloat()));
        for (local xx = x + s; xx > x; xx--)
            if (xx in rows[z]) ring.append(vertex(xx.tofloat(), z.tofloat()));
        local centre = vertex(x + s * 0.5, z + s * 0.5);
        for (local i = 0; i < ring.len(); i++)
            indices.extend([centre, ring[i], ring[(i + 1) % ring.len()]]);
    }
    adaptiveLeafCount = leaves.len();
    adaptiveVertexCount = pos.len() / 3;
    adaptiveTriangleCount = indices.len() / 3;
    if (adaptiveTopologyAudit) auditAdaptiveSnowMesh(pos, indices);
    if (terrainMesh == null) {
        terrainMesh = gfx.newMeshFromArrays(pos, normals, uv, adaptiveVertexCount, indices, indices.len());
        if (terrainMesh == null) throw "adaptive snow mesh creation failed";
    } else if (!gfx.updateMeshVertices(terrainMesh, pos, normals, uv, adaptiveVertexCount, indices, indices.len())) {
        throw "adaptive snow mesh update failed";
    }
}
