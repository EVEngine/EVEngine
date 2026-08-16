#version 450

layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

void main() {
    // Push-constant slots: 6=intensity 7=fogR 8=fogG 9=fogB 10=fogDensity.
    vec4 tex = texture(albedoSampler, vUV);
    float intensity = u.data[6];
    vec4 col = tex * vTint;
    col.a *= intensity;
    if (col.a < 0.5) discard;

    float viewDist = length(vViewPos);
    float fogAmt = clamp(1.0 - exp(-viewDist * u.data[10]), 0.0, 1.0);
    vec3 fogCol = vec3(u.data[7], u.data[8], u.data[9]);
    vec3 rgb = mix(col.rgb, fogCol, fogAmt);

    // Opaque pipeline: alpha carries linear depth for the scene color target.
    outColor = vec4(rgb, 1.0);
}
