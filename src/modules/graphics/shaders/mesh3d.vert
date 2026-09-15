#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;
layout(location = 5) in vec4 inColor;

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity; // xyz = primary dir; w = lightCount
    vec4 lightColor;        // rgb = primary; w = metallic
    vec4 tint;
    vec4 cameraPos;         // xyz = eye; w = roughness
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo; // near, far
    vec4 cloud;
    vec4 cloudWind;
    vec4 virtualTexture;
    vec4 virtualAtlas;
    vec4 bindlessEnv;
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;

layout(std430, set = 0, binding = 21) readonly buffer SkinPalette { mat4 bones[]; } skinPalette;

void main() {
    vec4 localPos = vec4(inPos, 1.0);
    vec3 localNormal = inNormal;
    if (ubo.skinInfo.x > 0.5) {
        vec4 weights = inWeights;
        if (ubo.skinInfo.y < 3.5) weights.zw = vec2(0.0);
        if (ubo.skinInfo.y < 1.5) weights.y = 0.0;
        float weightSum = dot(weights, vec4(1.0));
        if (weightSum > 1e-8) weights /= weightSum;
        mat4 skin = weights.x * skinPalette.bones[inJoints.x]
                  + weights.y * skinPalette.bones[inJoints.y]
                  + weights.z * skinPalette.bones[inJoints.z]
                  + weights.w * skinPalette.bones[inJoints.w];
        localPos = skin * localPos;
        localNormal = mat3(skin) * localNormal;
    }
    gl_Position = ubo.mvp * localPos;
    vec4 world = ubo.model * localPos;
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    mat3 normalMat = transpose(inverse(mat3(ubo.model)));
    vNormal = normalize(normalMat * localNormal);
    vUV = inUV;
    vTint = ubo.tint * inColor;
    vCameraPos = ubo.cameraPos.xyz;
}
