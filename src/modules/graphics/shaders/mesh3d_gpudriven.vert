#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"
#include "terrain_detail_wave.glsl"

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

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;
layout(location = 6) out flat uint vMaterialId;
layout(location = 7) out flat uint vInstanceId;

void main() {
    // gl_InstanceIndex already includes VkDrawIndexedIndirectCommand.firstInstance,
    // and the CPU writes instances in the sorted bucket order with
    // cmd.firstInstance = bucket start, so this is the direct buffer index.
    uint inst = gl_InstanceIndex;
    GpuInstance gi = instances[inst];
    GpuMaterialRecord m = materials[gi.materialId];

    vec4 world;
    if ((m.flags & 4u) != 0u) {
        vec3 origin = gi.model[3].xyz;
        vec3 toCamera = ubo.cameraPos.xyz - origin;
        toCamera.y = 0.0;
        toCamera = length(toCamera) > 1e-6 ? normalize(toCamera) : vec3(0, 0, 1);
        vec3 right = vec3(toCamera.z, 0, -toCamera.x);
        vec3 scale = vec3(length(gi.model[0].xyz), length(gi.model[1].xyz), length(gi.model[2].xyz));
        world = vec4(origin + right * inPos.x * scale.x + vec3(0, 1, 0) * inPos.y * scale.y +
                     toCamera * inPos.z * scale.z, 1.0);
    } else {
        world = gi.model * vec4(inPos, 1.0);
    }
    vec3 waveTint = terrainDetailWave(world.xyz, inUV.y, gi);
    gl_Position = ubo.mvp * world;
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    // Full inverse-transpose per vertex; correctness over speed until stage 2
    // moves LOD/instance prep to the GPU (precomputed normal matrices).
    if ((m.flags & 4u) != 0u) {
        vec3 facing = ubo.cameraPos.xyz - gi.model[3].xyz;
        facing.y = 0.0;
        vNormal = length(facing) > 1e-6 ? normalize(facing) : vec3(0, 0, 1);
    } else {
        mat3 normalMat = transpose(inverse(mat3(gi.model)));
        vNormal = normalize(normalMat * inNormal);
    }
    vUV = inUV;
    vTint = vec4(gi.color.rgb * waveTint, gi.color.a);
    vCameraPos = ubo.cameraPos.xyz;
    vMaterialId = gi.materialId;
    vInstanceId = inst;
}
