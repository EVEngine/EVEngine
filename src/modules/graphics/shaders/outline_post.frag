#version 450
// Screen-space model outline (t3ssel8r-style) from depth + normal.
//
// MainTex  (binding 0) = hardware D32 depth, .r = Vulkan NDC z (0 near, 1 far).
// NormalTex(binding 1) = GBuffer world normal * 0.5 + 0.5.
//
// For each pixel we sample the depth and normal of an 8-neighbourhood at the
// requested width. A pixel is flagged as an outline when either:
//   - the (view-space) depth discontinuity to a neighbour exceeds a threshold
//     scaled by distance (silhouettes, occluded edges), or
//   - the normal discontinuity (1 - dot(n, nN)) exceeds a threshold (creases).
// The pass emits vec4(outlineColor, edgeMask); drawn with SrcAlpha blending
// over the already-rendered scene this produces crisp ink outlines.
//
// Push constants (declareFloat order):
//   0 width  1 depthThreshold  2 depthSensitivity
//   3 normalThreshold  4 softness
//   5 colorR  6 colorG  7 colorB
//   8 texelW  9 texelH  10 nearZ  11 farZ

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D NormalTex;
layout(push_constant) uniform Externals { float data[32]; } u;

const vec2 uvOffsets[8] = vec2[8](
    vec2(1.0, 0.0), vec2(-1.0, 0.0),
    vec2(0.0, 1.0), vec2(0.0, -1.0),
    vec2(0.70710678, 0.70710678), vec2(-0.70710678, -0.70710678),
    vec2(-0.70710678, 0.70710678), vec2(0.70710678, -0.70710678));

float depthAt(vec2 uv) { return texture(MainTex, clamp(uv, 0.0, 1.0)).r; }

vec3 normalAt(vec2 uv) {
  vec3 n = texture(NormalTex, clamp(uv, 0.0, 1.0)).xyz * 2.0 - 1.0;
  float l = length(n);
  return l > 1e-3 ? n / l : vec3(0.0, 0.0, 1.0);
}

float toViewZ(float ndcZ) {
  float nearZ = max(u.data[10], 1e-4);
  float farZ = max(u.data[11], nearZ + 1e-3);
  return (nearZ * farZ) / max(farZ - ndcZ * (farZ - nearZ), 1e-6);
}

void main() {
  float width = max(u.data[0], 0.5);
  float depthThreshold = max(u.data[1], 0.0);
  float depthSensitivity = max(u.data[2], 0.0);
  float normalThreshold = clamp(u.data[3], 0.0, 2.0);
  float softness = clamp(u.data[4], 0.0, 1.0);
  vec3 color = vec3(u.data[5], u.data[6], u.data[7]);
  vec2 texel = vec2(max(u.data[8], 1e-5), max(u.data[9], 1e-5));

  float ndcZ = depthAt(fragUV);
  if (ndcZ >= 0.9999) {
    outColor = vec4(color, 0.0);
    return;
  }

  float dC = toViewZ(ndcZ);
  vec3 nC = normalAt(fragUV);

  float depthEdge = 0.0;
  float normalEdge = 0.0;
  for (int i = 0; i < 8; ++i) {
    vec2 off = uvOffsets[i] * texel * width;
    vec2 uvN = fragUV + off;
    // Cutoff grows with distance so outlines stay consistent in world space.
    float cutoff = depthThreshold + dC * depthSensitivity;
    float dd = abs(dC - toViewZ(depthAt(uvN)));
    depthEdge = max(depthEdge, smoothstep(cutoff * (1.0 - softness), cutoff, dd));
    float ndot = clamp(dot(nC, normalAt(uvN)), -1.0, 1.0);
    normalEdge = max(normalEdge, 1.0 - ndot);
  }

  float edge = max(depthEdge, smoothstep(normalThreshold * (1.0 - softness),
                                         normalThreshold + softness, normalEdge));
  outColor = vec4(color, clamp(edge, 0.0, 1.0));
}
