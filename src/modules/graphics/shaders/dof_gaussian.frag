#version 450
// Separable Gaussian depth-of-field for HD-2D miniature look.
//
// MainTex (binding 0) = linear HDR scene color.
// DepthTex(binding 1) = hardware D32 depth, .r = Vulkan NDC z (0 near, 1 far).
//
// Push constants (declareFloat order):
//   0 texelW  1 texelH  2 directionX  3 directionY
//   4 focusDistance  5 focusRange  6 maxBlurPx
//   7 nearZ  8 farZ

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float toViewZ(float ndcZ) {
  float nearZ = max(u.data[7], 1e-4);
  float farZ = max(u.data[8], nearZ + 1e-3);
  return (nearZ * farZ) / max(farZ - ndcZ * (farZ - nearZ), 1e-6);
}

float cocRadius(float viewZ) {
  float focus = max(u.data[4], 0.0);
  float range = max(u.data[5], 1e-3);
  float maxBlur = max(u.data[6], 0.0);
  float t = clamp(abs(viewZ - focus) / range, 0.0, 1.0);
  // Smoothstep keeps the in-focus band readable for pixel characters.
  t = t * t * (3.0 - 2.0 * t);
  return t * maxBlur;
}

void main() {
  float maxBlur = max(u.data[6], 0.0);
  if (maxBlur <= 1e-4) {
    outColor = texture(MainTex, fragUV) * fragColor;
    return;
  }

  float ndcZ = texture(DepthTex, fragUV).r;
  float viewZ = toViewZ(ndcZ);
  float radius = cocRadius(viewZ);
  if (radius < 0.35 || ndcZ >= 0.9999) {
    outColor = texture(MainTex, fragUV) * fragColor;
    return;
  }

  vec2 dir = vec2(u.data[2], u.data[3]);
  vec2 stepUv = dir * vec2(u.data[0], u.data[1]) * radius;

  // 9-tap Gaussian (sigma ~= 2) along the separable axis.
  const float kW[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
  vec3 color = texture(MainTex, fragUV).rgb * kW[0];
  float weight = kW[0];
  for (int i = 1; i < 5; ++i) {
    float fi = float(i);
    vec2 off = stepUv * fi;
    vec2 uvA = clamp(fragUV + off, vec2(0.0), vec2(1.0));
    vec2 uvB = clamp(fragUV - off, vec2(0.0), vec2(1.0));
    float zA = toViewZ(texture(DepthTex, uvA).r);
    float zB = toViewZ(texture(DepthTex, uvB).r);
    // Reject samples whose CoC is much smaller (keeps sharp sprites from bleeding).
    float wA = kW[i] * step(radius * 0.35, max(cocRadius(zA), 0.35));
    float wB = kW[i] * step(radius * 0.35, max(cocRadius(zB), 0.35));
    color += texture(MainTex, uvA).rgb * wA;
    color += texture(MainTex, uvB).rgb * wB;
    weight += wA + wB;
  }
  outColor = vec4(color / max(weight, 1e-4), 1.0) * fragColor;
}
