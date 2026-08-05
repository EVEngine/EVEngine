#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity; // xyz direction toward surface
    vec4 lightColor;
    vec4 tint;
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vLightDir;
layout(location = 4) out vec3 vLightColor;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    mat3 normalMat = mat3(ubo.model);
    vNormal = normalize(normalMat * inNormal);
    vUV = inUV;
    vTint = ubo.tint;
    vLightDir = normalize(ubo.lightDirIntensity.xyz);
    vLightColor = ubo.lightColor.rgb;
}
