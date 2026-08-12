#version 450
// Hair / fur card vertex — Frame UBO matches mesh3d_toon; outputs tangent for Kajiya-Kay.

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
    float data[32];
} u;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vTint;
layout(location = 3) out vec3 vLightDir;
layout(location = 4) out vec3 vLightColor;
layout(location = 5) out vec3 vWorldPos;
layout(location = 6) out vec3 vCameraPos;
layout(location = 7) out vec3 vTangent;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vec4 world = ubo.model * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    mat3 normalMat = mat3(ubo.model);
    vec3 N = normalize(normalMat * inNormal);
    vNormal = N;
    vUV = inUV;
    vTint = ubo.tint;
    vLightDir = normalize(ubo.lightDirIntensity.xyz);
    vLightColor = ubo.lightColor.rgb;
    vCameraPos = ubo.cameraPos.xyz;

    // Strand direction: push override (object-space xyz) or derive from UV-V axis on the card.
    vec3 strandObj = vec3(u.data[6], u.data[7], u.data[8]);
    if (dot(strandObj, strandObj) > 1e-6) {
        vTangent = normalize(normalMat * normalize(strandObj));
    } else {
        // Hair cards usually flow along increasing V — approximate tangent from position gradient.
        vec3 up = abs(N.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vTangent = normalize(cross(up, N));
    }
}
