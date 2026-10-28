#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 0) out vec4 outColor;

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
} ubo;

layout(push_constant) uniform Externals {
    float data[32];
} pc;

void main() {
    float time = pc.data[0];
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(vCameraPos - vWorldPos);
    vec3 lightDir = normalize(ubo.lightDirIntensity.xyz);
    float diffuse = max(dot(normal, lightDir), 0.0);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.5);
    float bands = 0.55 + 0.45 * sin(vWorldPos.y * 7.0 + time * 2.0);
    float grid = smoothstep(0.92, 1.0,
                            max(abs(sin(vUV.x * 31.4159)), abs(sin(vUV.y * 31.4159))));

    vec3 base = vTint.rgb * mix(0.48, 1.0, diffuse);
    base *= mix(0.82, 1.12, bands);
    base += rim * vec3(0.25, 0.65, 1.0);
    base = mix(base, vec3(1.0, 0.42, 0.16), grid * 0.18);
    base += ubo.ambient.rgb * vTint.rgb;
    outColor = vec4(base, vTint.a);
}
