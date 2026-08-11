#version 450

// Unit-quad corner in [0,1]^2 (per-vertex).
layout(location = 0) in vec2 inCorner;
// Packed rect instance (per-instance): xyz(5+5+5) + wh(5+5) + tex(7).
layout(location = 1) in uint inPacked;

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

void main() {
    uint packed = inPacked;
    uint ix = packed & 31u;
    uint iy = (packed >> 5) & 31u;
    uint iz = (packed >> 10) & 31u;
    float w = float((packed >> 15) & 31u) + 1.0;
    float h = float((packed >> 20) & 31u) + 1.0;
    uint tex = (packed >> 25) & 127u;

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

    float tiles = max(pc.atlasInfo.x, 1.0);
    float tileU = 1.0 / tiles;
    float col = mod(float(tex), tiles);
    float row = floor(float(tex) / tiles);
    vUV = vec2((col + inCorner.x) * tileU, (row + inCorner.y) * tileU);
    vTex = tex;
    vNormal = n;
    vTint = pc.tint;
}
