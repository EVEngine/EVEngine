#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

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
} ubo;

layout(set = 1, binding = 2, std430) readonly buffer Meshes {
    GpuMeshRecord meshes[];
};
layout(set = 1, binding = 3, std430) readonly buffer Materials {
    GpuMaterialRecord materials[];
};
layout(set = 1, binding = 4, std430) readonly buffer Instances {
    GpuInstance instances[];
};

layout(push_constant) uniform Push {
    uint firstInstance;
    uint pad0;
    uint pad1;
    uint pad2;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;
layout(location = 6) out flat uint vMaterialId;

void main() {
    uint inst = pc.firstInstance + gl_InstanceIndex;
    GpuInstance gi = instances[inst];
    GpuMaterialRecord m = materials[gi.materialId];

    gl_Position = ubo.mvp * (gi.model * vec4(inPos, 1.0));
    vec4 world = gi.model * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    // Full inverse-transpose per vertex; correctness over speed until stage 2
    // moves LOD/instance prep to the GPU (precomputed normal matrices).
    mat3 normalMat = transpose(inverse(mat3(gi.model)));
    vNormal = normalize(normalMat * inNormal);
    vUV = inUV;
    vTint = m.tint;
    vCameraPos = ubo.cameraPos.xyz;
    vMaterialId = gi.materialId;
}
