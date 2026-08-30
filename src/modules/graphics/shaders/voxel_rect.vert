#version 450

// Unit-quad corner in [0,1]^2 (per-vertex).
layout(location = 0) in vec2 inCorner;
// Packed rect instance (per-instance): xyz(5+5+5) + wh(5+5) + tex(7).
layout(location = 1) in uint inPacked;
// Per-instance AO word: 2 bits per corner (0..3), shader corner order.
layout(location = 2) in uint inAO;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 chunkOrigin; // xyz = world origin of chunk; w = faceDir (0..5)
    vec4 atlasInfo;   // x = tilesPerRow, y unused, z unused, w unused
    vec4 tint;        // rgba
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out flat uint vTex;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out vec4 vTint;
layout(location = 4) out float vAO;
layout(location = 5) out flat float vTiles;

void main() {
    uint packed = inPacked;
    uint ix = packed & 31u;
    uint iy = (packed >> 5) & 31u;
    uint iz = (packed >> 10) & 31u;
    float w = float((packed >> 15) & 31u) + 1.0;
    float h = float((packed >> 20) & 31u) + 1.0;
    uint tex = (packed >> 25) & 127u;

    uint ao0 = inAO & 3u;
    uint ao1 = (inAO >> 2) & 3u;
    uint ao2 = (inAO >> 4) & 3u;
    uint ao3 = (inAO >> 6) & 3u;
    uint ao = inCorner.y < 0.5 ? (inCorner.x < 0.5 ? ao0 : ao1)
                               : (inCorner.x < 0.5 ? ao3 : ao2);

    int face = int(pc.chunkOrigin.w + 0.5);
    vec3 pos = vec3(float(ix), float(iy), float(iz));
    vec3 n = vec3(0.0);

    // Place outer face plane (+dirs at voxel+1) and stretch along tangents.
    if (face == 0) { // +X
        pos = vec3(float(ix) + 1.0, float(iy) + inCorner.y * h, float(iz) + inCorner.x * w);
        n = vec3(1.0, 0.0, 0.0);
    } else if (face == 1) { // -X
        pos = vec3(float(ix), float(iy) + inCorner.y * h, float(iz) + (1.0 - inCorner.x) * w);
        n = vec3(-1.0, 0.0, 0.0);
    } else if (face == 2) { // +Y
        pos = vec3(float(ix) + inCorner.x * w, float(iy) + 1.0, float(iz) + inCorner.y * h);
        n = vec3(0.0, 1.0, 0.0);
    } else if (face == 3) { // -Y
        pos = vec3(float(ix) + inCorner.x * w, float(iy), float(iz) + (1.0 - inCorner.y) * h);
        n = vec3(0.0, -1.0, 0.0);
    } else if (face == 4) { // +Z
        pos = vec3(float(ix) + (1.0 - inCorner.x) * w, float(iy) + inCorner.y * h, float(iz) + 1.0);
        n = vec3(0.0, 0.0, 1.0);
    } else { // -Z
        pos = vec3(float(ix) + inCorner.x * w, float(iy) + inCorner.y * h, float(iz));
        n = vec3(0.0, 0.0, -1.0);
    }

    vec3 world = pos + pc.chunkOrigin.xyz;
    gl_Position = pc.viewProj * vec4(world, 1.0);

    // Keep UVs in voxel units. The fragment shader wraps them inside the selected
    // atlas tile, so greedy rectangles retain the same texel density as separate cubes.
    vUV = inCorner * vec2(w, h);
    vTex = tex;
    vNormal = n;
    vTint = pc.tint;
    vAO = float(ao) / 3.0;
    vTiles = max(pc.atlasInfo.x, 1.0);
}
