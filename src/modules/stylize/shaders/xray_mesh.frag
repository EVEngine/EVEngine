#version 450
// X-ray occlusion mesh fragment shader ("see people through buildings").
//
// Drawn as a SECOND pass over the already-rendered scene for entities flagged
// with X-ray. The backend builds this pipeline with depth test/write disabled
// and alpha blending, so occluded fragments can paint over the buildings.
//
// The fragment samples the G-buffer hardware depth at its own screen pixel
// (sceneDepth, set=0 binding=7; D32, Vulkan NDC z, 0 = near, 1 = far) and
// compares it against its own fragment depth (gl_FragCoord.z, same space).
//   - occluded  (own depth is BEHIND the occluder surface): emit the X-ray
//                highlight color as a filled silhouette.
//   - visible   (own depth is at / in front of the surface): discard, leaving
//                the normal material from the first pass on screen.
//
// Binding 1 (albedo) and the Frame UBO (binding 0) come from the shared
// mesh3d layout so we can reuse mesh3d_toon.vert unchanged.
//
// Push constants (declareFloat order in bindMeshUniforms("xray")):
//   0 colorR  1 colorG  2 colorB  3 bias
//   4 screenW 5 screenH
//   6 rimPower 7 rimStrength 8 alpha

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 7) uniform sampler2D sceneDepth;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = vec3(u.data[0], u.data[1], u.data[2]);
    float bias = max(u.data[3], 0.0);
    float screenW = max(u.data[4], 1.0);
    float screenH = max(u.data[5], 1.0);
    float rimPower = max(u.data[6], 0.1);
    float rimStrength = clamp(u.data[7], 0.0, 1.0);
    float alpha = clamp(u.data[8], 0.0, 1.0);

    vec2 uv = gl_FragCoord.xy / vec2(screenW, screenH);
    float sceneZ = texture(sceneDepth, uv).r;

    // 0 = surface in front of us (visible), 1 = we are behind it (occluded).
    float occ = smoothstep(sceneZ, sceneZ + bias, gl_FragCoord.z);
    if (occ < 0.5) discard;  // visible part -> keep the normal material

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), rimPower);
    float edge = mix(1.0, rim, rimStrength);

    outColor = vec4(color, alpha * edge * occ);
}
