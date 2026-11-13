#version 450
// Static Mesh3D vessel path. Local coordinates make the glaze line independent
// of object placement, rotation and nonuniform scale.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 5) in vec4 inColor;
struct Light3D {
    vec4 posRadius;
    vec4 color;
};
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4    mvp;
    mat4    model;
    vec4    lightDirIntensity;
    vec4    lightColor;
    vec4    tint;
    vec4    cameraPos;
    vec4    ambient;
    Light3D lights[8];
}
ubo;
layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out vec3 vCameraPos;
layout(location = 6) out vec3 vLocalPos;
void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vWorldPos   = (ubo.model * vec4(inPos, 1.0)).xyz;
    vNormal     = normalize(transpose(inverse(mat3(ubo.model))) * inNormal);
    vUV         = inUV;
    vTint       = ubo.tint * inColor;
    vCameraPos  = ubo.cameraPos.xyz;
    vLocalPos   = inPos;
}
