#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"

// Stage-3 virtual-geometry visibility pass. No vertex input: each draw is one
// cluster with vertexCount = cluster.triCount * 3; gl_InstanceIndex is the
// cluster id and gl_VertexIndex indexes triangle corners. Positions come from
// the shared cluster position stream; the face normal is computed from the
// world-space triangle edges (exact match for the resolve).

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
    vec4 cloud;
    vec4 cloudWind;
    vec4 bindlessEnv;
} ubo;

layout(set = 1, binding = 22, std430) readonly buffer VgPositions {
    float vgPos[];
};
layout(set = 1, binding = 23, std430) readonly buffer VgTriangles {
    uint vgTri[];
};
layout(set = 1, binding = 24, std430) readonly buffer VgClusters {
    uvec4 vgCl[];
};
layout(set = 1, binding = 25, std430) readonly buffer VgClusterAssets {
    uint vgClusterAssets[];
};
layout(set = 1, binding = 29, std430) readonly buffer VgAssetModels {
    mat4 vgAssetModels[];
};

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out float vNdcZ;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vBary;
layout(location = 4) out vec3 vWorldPos;
layout(location = 5) out flat uint vClusterId;
layout(location = 6) out flat uint vTriBase;

void main() {
    uint cid = gl_InstanceIndex;
    uint asset = vgClusterAssets[cid];
    mat4 model = vgAssetModels[asset];
    uvec4 rng = vgCl[cid * 4u + 1u];
    uint triStart = rng.x;
    uint tri = gl_VertexIndex / 3u;
    uint corner = gl_VertexIndex % 3u;
    uint base = (triStart + tri) * 3u;

    uint i0 = vgTri[base + 0u];
    uint i1 = vgTri[base + 1u];
    uint i2 = vgTri[base + 2u];
    vec3 p0 = vec3(vgPos[3u * i0 + 0u], vgPos[3u * i0 + 1u], vgPos[3u * i0 + 2u]);
    vec3 p1 = vec3(vgPos[3u * i1 + 0u], vgPos[3u * i1 + 1u], vgPos[3u * i1 + 2u]);
    vec3 p2 = vec3(vgPos[3u * i2 + 0u], vgPos[3u * i2 + 1u], vgPos[3u * i2 + 2u]);

    vec4 w0 = model * vec4(p0, 1.0);
    vec4 w1 = model * vec4(p1, 1.0);
    vec4 w2 = model * vec4(p2, 1.0);
    vec4 world = corner == 0u ? w0 : (corner == 1u ? w1 : w2);

    vWorldPos = world.xyz;
    gl_Position = ubo.mvp * world;
    // Face normal from world edges; identical for all three corners.
    vWorldNormal = normalize(cross(w1.xyz - w0.xyz, w2.xyz - w0.xyz));
    vNdcZ = gl_Position.z / max(gl_Position.w, 1e-6);
    vUV = vec2(0.0);
    vBary = vec3(corner == 0u ? 1.0 : 0.0, corner == 1u ? 1.0 : 0.0, corner == 2u ? 1.0 : 0.0);
    vClusterId = cid;
    vTriBase = base;
}
