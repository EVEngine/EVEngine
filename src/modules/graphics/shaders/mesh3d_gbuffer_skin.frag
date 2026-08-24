#version 450

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in float vNdcZ;
layout(location = 2) in vec2 vUV;

layout(std140, set = 0, binding = 0) uniform SkinPass {
    mat4 mvp;
    mat4 model;
    vec4 clip;
    vec4 skinInfo;
    mat4 skinBones[128];
} skinPass;
layout(set = 0, binding = 1) uniform sampler2D MainTex;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outDepth;
layout(location = 2) out vec4 outAlbedo;

void main() {
    vec3 n = normalize(vWorldNormal);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);
    float z = clamp(vNdcZ, 0.0, 1.0);
    float nearZ = max(skinPass.clip.x, 1e-4);
    float farZ = max(skinPass.clip.y, nearZ + 1e-4);
    float zEye = (nearZ * farZ) / max(farZ - z * (farZ - nearZ), 1e-6);
    float linear01 = clamp((zEye - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outDepth = vec4(linear01);
    uint packedTint = floatBitsToUint(skinPass.clip.z);
    vec3 tint = vec3(float(packedTint & 255u), float((packedTint >> 8) & 255u),
                     float((packedTint >> 16) & 255u)) / 255.0;
    outAlbedo = vec4(texture(MainTex, vUV).rgb * tint, linear01);
}
