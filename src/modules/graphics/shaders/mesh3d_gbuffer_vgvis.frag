#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in float vNdcZ;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vBary;
layout(location = 4) in vec3 vWorldPos;
layout(location = 5) in flat uint vClusterId;
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
} ubo;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outDepth;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out uvec2 outVisID;
layout(location = 4) out vec2 outVisBary;

void main() {
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
    // Flat placeholder albedo; the resolve does the actual (flat) shading.
    outAlbedo = vec4(vec3(0.8), linear01);

    // bit31 = VG marker; low 31 bits = global cluster id.
    outVisID = uvec2(0x80000000u | (vClusterId & 0x7FFFFFFFu), vTriBase);
    outVisBary = vBary.xy;
}
