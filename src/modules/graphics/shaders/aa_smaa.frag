#version 450
// SMAA-inspired single-pass morphological AA (Jimenez et al.).
// Detects luma edges, searches along the dominant edge for pattern length,
// then blends across the edge — fits the engine's single-texture post path.
// Push constants (declareFloat order):
//  0 texelW, 1 texelH
//  2 threshold          — edge detection threshold
//  3 localContrast      — local contrast adaptation factor
//  4 maxSearch          — max search steps (quality)
//  5 cornerRounding     — diagonal / corner blend amount

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float searchX(vec2 uv, vec2 texel, float dir, float threshold, int maxSteps) {
  float dist = 0.0;
  for (int i = 1; i <= 16; ++i) {
    if (i > maxSteps) break;
    dist = float(i);
    vec2 p = uv + vec2(dir * texel.x * dist, 0.0);
    float l0 = luma(texture(MainTex, p + vec2(0.0, -0.25 * texel.y)).rgb);
    float l1 = luma(texture(MainTex, p + vec2(0.0,  0.25 * texel.y)).rgb);
    if (abs(l0 - l1) < threshold) break;
  }
  return dist;
}

float searchY(vec2 uv, vec2 texel, float dir, float threshold, int maxSteps) {
  float dist = 0.0;
  for (int i = 1; i <= 16; ++i) {
    if (i > maxSteps) break;
    dist = float(i);
    vec2 p = uv + vec2(0.0, dir * texel.y * dist);
    float l0 = luma(texture(MainTex, p + vec2(-0.25 * texel.x, 0.0)).rgb);
    float l1 = luma(texture(MainTex, p + vec2( 0.25 * texel.x, 0.0)).rgb);
    if (abs(l0 - l1) < threshold) break;
  }
  return dist;
}

void main() {
  vec2 texel = vec2(u.data[0], u.data[1]);
  float threshold = max(u.data[2], 1e-4);
  float localContrast = max(u.data[3], 0.0);
  int maxSearch = int(clamp(u.data[4], 4.0, 16.0));
  float cornerRounding = clamp(u.data[5], 0.0, 1.0);

  vec3 cM = texture(MainTex, fragUV).rgb;
  float lM = luma(cM);
  float lL = luma(texture(MainTex, fragUV + vec2(-texel.x, 0.0)).rgb);
  float lR = luma(texture(MainTex, fragUV + vec2( texel.x, 0.0)).rgb);
  float lT = luma(texture(MainTex, fragUV + vec2(0.0, -texel.y)).rgb);
  float lB = luma(texture(MainTex, fragUV + vec2(0.0,  texel.y)).rgb);

  vec4 delta = abs(vec4(lM - lL, lM - lT, lM - lR, lM - lB));
  vec2 edges = step(threshold, delta.xy);
  // Local contrast adaptation (SMAA predication-like): keep only strongest side.
  float maxDelta = max(max(delta.x, delta.y), max(delta.z, delta.w));
  edges *= step(maxDelta * localContrast, delta.xy);

  if (dot(edges, vec2(1.0)) < 1e-4) {
    outColor = vec4(cM * fragColor.rgb, 1.0);
    return;
  }

  vec4 weights = vec4(0.0);

  if (edges.x > 0.5) {
    // Vertical edge on left → horizontal blend
    float d1 = searchY(fragUV, texel, -1.0, threshold, maxSearch);
    float d2 = searchY(fragUV, texel,  1.0, threshold, maxSearch);
    float len = d1 + d2;
    float offset = (len > 1e-4) ? (0.5 - d1 / len) : 0.0;
    // Quadratic falloff like SMAA area weights (analytical approx).
    float w = abs(offset) * 2.0;
    w = 1.0 - w * w;
    weights.x = w;
    weights.y = w;
  }

  if (edges.y > 0.5) {
    // Horizontal edge on top → vertical blend
    float d1 = searchX(fragUV, texel, -1.0, threshold, maxSearch);
    float d2 = searchX(fragUV, texel,  1.0, threshold, maxSearch);
    float len = d1 + d2;
    float offset = (len > 1e-4) ? (0.5 - d1 / len) : 0.0;
    float w = abs(offset) * 2.0;
    w = 1.0 - w * w;
    weights.z = w;
    weights.w = w;
  }

  // Diagonal / corner rounding
  float lTL = luma(texture(MainTex, fragUV + vec2(-texel.x, -texel.y)).rgb);
  float lTR = luma(texture(MainTex, fragUV + vec2( texel.x, -texel.y)).rgb);
  float lBL = luma(texture(MainTex, fragUV + vec2(-texel.x,  texel.y)).rgb);
  float lBR = luma(texture(MainTex, fragUV + vec2( texel.x,  texel.y)).rgb);
  float diagA = abs((lTL + lBR) - (lTR + lBL));
  float diagBoost = smoothstep(threshold, threshold * 3.0, diagA) * cornerRounding;
  weights *= (1.0 + diagBoost);

  float sum = weights.x + weights.y + weights.z + weights.w;
  if (sum < 1e-4) {
    outColor = vec4(cM * fragColor.rgb, 1.0);
    return;
  }
  weights /= sum;

  vec3 cL = texture(MainTex, fragUV + vec2(-texel.x, 0.0)).rgb;
  vec3 cR = texture(MainTex, fragUV + vec2( texel.x, 0.0)).rgb;
  vec3 cT = texture(MainTex, fragUV + vec2(0.0, -texel.y)).rgb;
  vec3 cB = texture(MainTex, fragUV + vec2(0.0,  texel.y)).rgb;

  // Neighborhood blend: mix center with edge neighbors by SMAA-like weights.
  vec3 blended = cM * (1.0 - 0.5 * (weights.x + weights.y + weights.z + weights.w))
               + 0.5 * (cL * weights.x + cR * weights.y + cT * weights.z + cB * weights.w);
  // Re-normalize toward a stable energy
  float keep = clamp(1.0 - 0.35 * (weights.x + weights.y + weights.z + weights.w), 0.35, 1.0);
  vec3 rgb = mix(blended, cM, 1.0 - keep);

  outColor = vec4(rgb * fragColor.rgb, 1.0);
}
