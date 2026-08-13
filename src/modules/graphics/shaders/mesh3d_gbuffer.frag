#version 450

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in float vNdcZ;
layout(location = 2) in vec2 vUV;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 modelR0;
    vec4 modelR1;
    vec4 modelR2;
    vec4 clip; // x=near, y=far
} pc;

layout(binding = 0) uniform sampler2D MainTex;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outDepth;
layout(location = 2) out vec4 outAlbedo;

void main() {
    vec3 n = normalize(vWorldNormal);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);

    float z = clamp(vNdcZ, 0.0, 1.0);
    float nearZ = max(pc.clip.x, 1e-4);
    float farZ = max(pc.clip.y, nearZ + 1e-4);
    float zEye = (nearZ * farZ) / max(farZ - z * (farZ - nearZ), 1e-6);
    float linear01 = clamp((zEye - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outDepth = vec4(linear01, linear01, linear01, 1.0);
    vec3 albedo = texture(MainTex, vUV).rgb;
    uint packedTint = floatBitsToUint(pc.clip.z);
    vec3 tint = vec3(float(packedTint & 255u), float((packedTint >> 8) & 255u),
                     float((packedTint >> 16) & 255u)) / 255.0;
    // A = linear depth so SSGI can sample albedo+depth with one sampler.
    outAlbedo = vec4(albedo * tint, linear01);
}
