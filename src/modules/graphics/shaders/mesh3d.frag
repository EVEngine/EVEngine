#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = texture(albedo, vUV) * vTint;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vLightDir);
    float ndotl = max(dot(N, L), 0.0);
    float ambient = 0.15;
    vec3 lit = base.rgb * (ambient + ndotl * vLightColor);
    outColor = vec4(lit, base.a);
}
