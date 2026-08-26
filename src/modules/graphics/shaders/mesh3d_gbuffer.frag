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
    float z = clamp(vNdcZ, 0.0, 1.0);
    float nearZ = max(pc.clip.x, 1e-4);
    float farZ = max(pc.clip.y, nearZ + 1e-4);
    float zEye = (nearZ * farZ) / max(farZ - z * (farZ - nearZ), 1e-6);
    float linear01 = clamp((zEye - nearZ) / (farZ - nearZ), 0.0, 1.0);
    uint packedMotion = uint(pc.clip.w + 0.5);
    vec2 motion = (vec2(packedMotion & 4095u, (packedMotion >> 12) & 4095u) - 2047.0) / 2047.0;
    uint packedTint = uint(pc.clip.z);
    uint pbr = ((packedTint >> 18) & 7u) | (((packedTint >> 21) & 7u) << 3);
    outNormal = vec4(n * 0.5 + 0.5, float(pbr) / 255.0);
    outDepth = vec4(linear01, clamp(motion * 0.5 + 0.5, 0.0, 1.0), 1.0);
    vec3 albedo = texture(MainTex, vUV).rgb;
    vec3 tint = vec3(float(packedTint & 63u), float((packedTint >> 6) & 63u),
                     float((packedTint >> 12) & 63u)) / 63.0;
    // A = linear depth so SSGI can sample albedo+depth with one sampler.
    outAlbedo = vec4(albedo * tint, linear01);
}
