#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in flat uint vTex;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec4 vTint;

layout(set = 0, binding = 0) uniform sampler2D atlasSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 albedo = texture(atlasSampler, vUV);
    // Simple Lambert toward +Y-ish key light for readability without a full UBO.
    vec3 L = normalize(vec3(0.35, 1.0, 0.25));
    float ndl = max(dot(normalize(vNormal), L), 0.0);
    float shade = 0.35 + 0.65 * ndl;
    outColor = vec4(albedo.rgb * vTint.rgb * shade, albedo.a * vTint.a);
    if (outColor.a < 0.01) discard;
}
