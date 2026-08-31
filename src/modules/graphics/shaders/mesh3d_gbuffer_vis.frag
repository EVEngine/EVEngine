#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"
#include "tex_cell_bomb.glsl"
#include "parallax_map.glsl"

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in float vNdcZ;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vBary;
layout(location = 4) in vec3 vWorldPos;
layout(location = 5) in flat uint vInstanceId;
layout(location = 6) in flat uint vTriBase;

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
    vec4 virtualTexture;
    vec4 virtualAtlas;
    vec4 bindlessEnv;
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
    mat4 skinBones[128];
    vec4 reflectionProbeCenter[2];
    vec4 reflectionProbeExtent[2];
} ubo;

layout(set = 1, binding = 0) uniform sampler2D textures[1024];
layout(set = 1, binding = 1) uniform samplerCube cubemaps[64];
layout(set = 1, binding = 2, std430) readonly buffer Meshes {
    GpuMeshRecord meshes[];
};
layout(set = 1, binding = 3, std430) readonly buffer Materials {
    GpuMaterialRecord materials[];
};
layout(set = 1, binding = 4, std430) readonly buffer Instances {
    GpuInstance instances[];
};

layout(set = 0, binding = 4, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits;
    vec4 bias;
    vec4 cascadeBias;
    vec4 cascadeTexel;
} shadow;

layout(set = 0, binding = 5) uniform sampler2DArrayShadow shadowMap;

#include "pbr_shade.glsl"

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outDepth;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out uvec2 outVisID;
layout(location = 4) out vec2 outVisBary;

void main() {
    uint inst = vInstanceId;
    GpuInstance gi = instances[inst];
    GpuMeshRecord mesh = meshes[gi.meshId];
    GpuMaterialRecord m = materials[gi.materialId];

    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;
    vec4 base = fetchAlbedo(m, N, V, vWorldPos, vUV);
    if (base.a < 0.5)
        discard;

    uint rough3 = uint(round(clamp(ubo.cameraPos.w, 0.0, 1.0) * 7.0));
    uint metal3 = uint(round(clamp(ubo.ambient.w, 0.0, 1.0) * 7.0));
    outNormal = vec4(normalize(vWorldNormal) * 0.5 + 0.5,
                     float(rough3 | (metal3 << 3)) / 255.0);

    float z = clamp(vNdcZ, 0.0, 1.0);
    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-4);
    float zEye = (nearZ * farZ) / max(farZ - z * (farZ - nearZ), 1e-6);
    float linear01 = clamp((zEye - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outDepth = vec4(linear01, 0.5, 0.5, 1.0);
    // A = linear depth so SSGI can sample albedo+depth with one sampler.
    outAlbedo = vec4(base.rgb, linear01);

    // visID.x = compacted instance index; visID.y = pooled index offset of the
    // triangle's first index.
    outVisID = uvec2(inst, vTriBase);
    outVisBary = vBary.xy;
}
