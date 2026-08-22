#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 5, std140) uniform Camera {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 nearFarTexel; // x = near, y = far, z = 1/width, w = 1/height
} cam;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 uvRect;    // atlas region [x, y, w, h]
    vec4 fadeParams;   // x = fade, y = normalStrength, z = roughStrength, w = metalStrength
    vec4 extraParams;  // x = emissiveStrength, y = blendMode, z/w unused
} pc;

void main() {
    gl_Position = cam.viewProj * pc.model * vec4(aPos, 1.0);
}
