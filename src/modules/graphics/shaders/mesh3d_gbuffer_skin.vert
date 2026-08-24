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
    mat4 skinBones[128];
} skinPass;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out float vNdcZ;
layout(location = 2) out vec2 vUV;

void main() {
    mat4 skin = inWeights.x * skinPass.skinBones[inJoints.x]
              + inWeights.y * skinPass.skinBones[inJoints.y]
              + inWeights.z * skinPass.skinBones[inJoints.z]
              + inWeights.w * skinPass.skinBones[inJoints.w];
    vec4 hp = skinPass.mvp * skin * vec4(inPos, 1.0);
    gl_Position = hp;
    mat3 skinNormal = mat3(skin);
    mat3 modelNormal = transpose(inverse(mat3(skinPass.model)));
    vWorldNormal = normalize(modelNormal * skinNormal * inNormal);
    vNdcZ = hp.z / max(hp.w, 1e-6);
    vUV = inUV;
}
