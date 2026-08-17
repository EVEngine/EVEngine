#version 450
// Sprite-stack slice vertex shader.
// MeshVertex layout (pos/normal/uv) and Frame UBO prefix match mesh3d, so
// Graphics::drawMeshShader can bind the shared descriptor set.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform Externals {
    vec4 uvRect;
    float data[28];
} u;

layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vUV = u.uvRect.xy + inUV * (u.uvRect.zw - u.uvRect.xy);
    vTint = ubo.tint;
}
