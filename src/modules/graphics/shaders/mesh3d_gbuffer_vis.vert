#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"

// Stage-3 visibility pass vertex shader. No vertex input binding: the mesh is
// fetched from the pooled vertex/index buffers (set 1) using gl_VertexIndex,
// so the draw is NON-indexed with vertexCount = mesh.indexCount. The barycentric
// constants are exact because gl_VertexIndex cycles 0,1,2 per triangle.

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

layout(set = 1, binding = 2, std430) readonly buffer Meshes {
    GpuMeshRecord meshes[];
};
layout(set = 1, binding = 4, std430) readonly buffer Instances {
    GpuInstance instances[];
};
layout(set = 1, binding = 18, std430) readonly buffer VertexPositions {
    vec4 positions[];
};
layout(set = 1, binding = 19, std430) readonly buffer VertexNormals {
    vec4 normals[];
};
layout(set = 1, binding = 20, std430) readonly buffer VertexUvs {
    vec2 uvs[];
};
layout(set = 1, binding = 21, std430) readonly buffer IndexPool {
    uint indices[];
};

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out float vNdcZ;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vBary;
layout(location = 4) out vec3 vWorldPos;
layout(location = 5) out flat uint vInstanceId;
layout(location = 6) out flat uint vTriBase;

void main() {
    // gl_InstanceIndex already includes VkDrawIndirectCommand.firstInstance and
    // the emit pass wrote compacted[bucketOffset + i] = instance i.
    uint inst = gl_InstanceIndex;
    GpuInstance gi = instances[inst];
    GpuMeshRecord mesh = meshes[gi.meshId];
    vInstanceId = inst;
    // Triangle base as a pooled index offset. Passed flat from the vertex
    // stage so the fragment does not need gl_PrimitiveID (which would pull in
    // the SPIR-V Geometry capability, requiring the geometryShader feature).
    vTriBase = mesh.indexOffset + (gl_VertexIndex / 3u) * 3u;

    uint vi = indices[mesh.indexOffset + gl_VertexIndex];
    vec3 pos = positions[mesh.vertexOffset + vi].xyz;
    vec3 nrm = normals[mesh.vertexOffset + vi].xyz;
    vec2 uv = uvs[mesh.vertexOffset + vi].xy;

    vec4 world = gi.model * vec4(pos, 1.0);
    vWorldPos = world.xyz;
    gl_Position = ubo.mvp * world;
    // Full inverse-transpose per vertex; matches the forward shader exactly.
    mat3 normalMat = transpose(inverse(mat3(gi.model)));
    vWorldNormal = normalize(normalMat * nrm);
    vNdcZ = gl_Position.z / max(gl_Position.w, 1e-6);
    vUV = uv;

    uint vid = gl_VertexIndex % 3u;
    vBary = vec3(vid == 0u ? 1.0 : 0.0, vid == 1u ? 1.0 : 0.0, vid == 2u ? 1.0 : 0.0);
}
