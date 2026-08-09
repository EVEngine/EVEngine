#version 450
// Object-space ink-wash for Mesh3D (sumi-e interior + silhouette).
// Same Frame UBO + albedo as mesh3d / mesh3d_toon.
// Push constants:
//   0 washLevels, 1 edgeThreshold, 2 inkDensity, 3 contrast,
//   4 paperR, 5 paperG, 6 paperB, 7 rimBoost

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(push_constant) uniform Externals {
  float data[32];
} u;

layout(location = 0) out vec4 outColor;

float luma(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  float washLevels = max(u.data[0], 2.0);
  float edgeThreshold = max(u.data[1], 0.05);
  float inkDensity = u.data[2];
  float contrast = max(u.data[3], 0.1);
  vec3 paper = vec3(u.data[4], u.data[5], u.data[6]);
  float rimBoost = u.data[7];

  vec4 base = texture(albedo, vUV) * vTint;
  vec3 N = normalize(vNormal);
  vec3 L = normalize(vLightDir);
  vec3 V = normalize(vCameraPos - vWorldPos);

  float ndotl = max(dot(N, L), 0.0);
  float fresnel = 1.0 - max(dot(N, V), 0.0);

  // Interior wash from lighting + albedo luminance.
  float shade = pow(clamp(ndotl * 0.75 + 0.25, 0.0, 1.0), contrast);
  shade *= mix(0.55, 1.0, luma(base.rgb));
  shade = floor(shade * washLevels + 1e-4) / max(washLevels - 1.0, 1.0);

  // Silhouette contour (view-normal).
  float silhouette = 1.0 - smoothstep(edgeThreshold, edgeThreshold + 0.25, fresnel);
  float rimInk = pow(clamp(fresnel, 0.0, 1.0), 2.0) * rimBoost;

  float ink = (1.0 - shade) * inkDensity + (1.0 - silhouette) + rimInk;
  ink = clamp(ink, 0.0, 1.0);

  vec3 inkCol = vec3(0.04, 0.04, 0.06);
  vec3 col = mix(paper, inkCol, ink);
  // Keep a hint of material tint in mid-wash.
  col = mix(col, col * mix(vec3(1.0), base.rgb, 0.25), shade);

  outColor = vec4(col * mix(vec3(1.0), vLightColor, 0.15), base.a);
}
