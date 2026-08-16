#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp; mat4 model;
    vec4 lightDirIntensity; vec4 lightColor; vec4 tint; vec4 cameraPos;
    vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo;
} ubo;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;

void main() {
    vec4 world = ubo.model * vec4(inPos, 1.0);
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    vUV = inUV;
    vTint = ubo.tint;
    vCameraPos = ubo.cameraPos.xyz;
    vNormal = inNormal;
}
