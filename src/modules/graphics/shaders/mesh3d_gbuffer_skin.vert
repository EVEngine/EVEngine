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

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out float vNdcZ;
layout(location = 2) out vec2 vUV;

layout(std430, set = 0, binding = 2) readonly buffer SkinPalette { mat4 bones[]; } skinPalette;

void main() {
    vec4 weights = inWeights;
    if (skinPass.skinInfo.y < 3.5) weights.zw = vec2(0.0);
    if (skinPass.skinInfo.y < 1.5) weights.y = 0.0;
    float weightSum = dot(weights, vec4(1.0));
    if (weightSum > 1e-8) weights /= weightSum;
    mat4 skin = weights.x * skinPalette.bones[inJoints.x]
              + weights.y * skinPalette.bones[inJoints.y]
              + weights.z * skinPalette.bones[inJoints.z]
              + weights.w * skinPalette.bones[inJoints.w];
    vec4 hp = skinPass.mvp * skin * vec4(inPos, 1.0);
    gl_Position = hp;
    mat3 skinNormal = mat3(skin);
    mat3 modelNormal = transpose(inverse(mat3(skinPass.model)));
    vWorldNormal = normalize(modelNormal * skinNormal * inNormal);
    vNdcZ = hp.z / max(hp.w, 1e-6);
    vUV = inUV;
}
