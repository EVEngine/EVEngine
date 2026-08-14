#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    mat4 view;
    vec4 lightDir;    // xyz = primary dir toward surface; w = 1 if enabled
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;   // xyz = eye; w = roughness
    vec4 ambient;     // rgb; w = metallic
    vec4 gridInfo;    // tilesX, tilesY, slices, pointCount
    vec4 clipInfo;    // near, far, screenW, screenH
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 5) out vec3 vViewPos;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vec4 world = ubo.model * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    mat3 normalMat = transpose(inverse(mat3(ubo.model)));
    vNormal = normalize(normalMat * inNormal);
    vUV = inUV;
    vTint = ubo.tint;
    vCameraPos = ubo.cameraPos.xyz;
}
