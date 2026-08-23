#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 5, std140) uniform Camera {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 nearFarTexel; // x = near, y = far, z = 1/width, w = 1/height
} cam;

struct DecalInstanceData {
    mat4 model;
    vec4 uvRect;      // atlas region [x, y, w, h]
    vec4 fadeParams;  // x = fade, y = normalStrength, z = roughStrength, w = metalStrength
    vec4 extraParams; // x = emissiveStrength, y = blendMode, z/w unused
};
layout(set = 0, binding = 6, std140) readonly buffer DecalInstances {
    DecalInstanceData instances[];
} inst;

layout(location = 0) flat out vec4 vUV;
layout(location = 1) flat out vec4 vFade;
layout(location = 2) flat out vec4 vExtra;
layout(location = 3) flat out int vInstance;

void main() {
    DecalInstanceData d = inst.instances[gl_InstanceIndex];
    gl_Position = cam.viewProj * d.model * vec4(aPos, 1.0);
    vUV = d.uvRect;
    vFade = d.fadeParams;
    vExtra = d.extraParams;
    vInstance = gl_InstanceIndex;
}
