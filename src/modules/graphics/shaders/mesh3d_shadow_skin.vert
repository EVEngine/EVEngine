#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(std140, set = 0, binding = 0) uniform SkinPass {
    mat4 mvp;
    mat4 model;
    vec4 clip;
    vec4 skinInfo;
} skinPass;

layout(location = 0) out vec2 vUV;

layout(std430, set = 0, binding = 2) readonly buffer SkinPalette { mat4 bones[]; } skinPalette;

void main() {
    mat4 skin = inWeights.x * skinPalette.bones[inJoints.x]
              + inWeights.y * skinPalette.bones[inJoints.y]
              + inWeights.z * skinPalette.bones[inJoints.z]
              + inWeights.w * skinPalette.bones[inJoints.w];
    gl_Position = skinPass.mvp * skin * vec4(inPos, 1.0);
    vUV = inUV;
}
