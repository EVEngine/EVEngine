#version 450
// Mesh3D vertex for stylized shaders — same UBO as mesh3d.vert, plus world/camera for rim.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos; // xyz
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vLightDir;
layout(location = 4) out vec3 vLightColor;
layout(location = 5) out vec3 vWorldPos;
layout(location = 6) out vec3 vCameraPos;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vec4 world = ubo.model * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    mat3 normalMat = transpose(inverse(mat3(ubo.model)));
    vNormal = normalize(normalMat * inNormal);
    vUV = inUV;
    vTint = ubo.tint;
    vLightDir = normalize(ubo.lightDirIntensity.xyz);
    vLightColor = ubo.lightColor.rgb;
    vCameraPos = ubo.cameraPos.xyz;
}
