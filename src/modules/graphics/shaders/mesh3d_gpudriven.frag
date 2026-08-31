#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"
#include "tex_cell_bomb.glsl"
#include "parallax_map.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 6) in flat uint vMaterialId;
layout(location = 7) in flat uint vInstanceId;

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
    vec4 bindlessEnv;  // x = env cubemap slot, y = envIntensity (appended; legacy ignores)
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
    mat4 skinBones[128];
    vec4 reflectionProbeCenter[2];
    vec4 reflectionProbeExtent[2];
} ubo;

layout(set = 0, binding = 4, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits;
    vec4 bias;
    vec4 cascadeBias;
    vec4 cascadeTexel;
} shadow;

layout(set = 0, binding = 5) uniform sampler2DArrayShadow shadowMap;
#define EVE_RAW_SHADOW_MAP 1
layout(set = 0, binding = 20) uniform sampler2DArray shadowMapRaw;

// Bindless set1
layout(set = 1, binding = 0) uniform sampler2D textures[1024];
layout(set = 1, binding = 1) uniform samplerCube cubemaps[64];
layout(set = 1, binding = 3, std430) readonly buffer Materials {
    GpuMaterialRecord materials[];
};
layout(set = 1, binding = 4, std430) readonly buffer Instances {
    GpuInstance instances[];
};

#include "pbr_shade.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    GpuMaterialRecord m = materials[vMaterialId];
    GpuInstance gi = instances[vInstanceId];
    vec3 Ngeom = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(Ngeom, V) < 0.0)
        Ngeom = -Ngeom;
    vec3 color = shadeGpuDrivenPixel(m, gi, Ngeom, V, vWorldPos, vViewPos, vUV);

    outColor = vec4(color, 1.0);
}
