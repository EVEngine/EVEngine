#version 450
// Temporal AA resolve. MainTex is the current frame and DepthTex is history.
// History is clipped to the current 3x3 color neighborhood before accumulation.
// Push: 0 texelW, 1 texelH, 2 blendCurrent, 3 clampAmount, 4 historyValid,
//       5 jitterDeltaX, 6 jitterDeltaY (history UV offset).
//       7..22 previousVP * inverse(currentVP), 23 near, 24 far, 25 reprojectionValid.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
layout(binding = 2) uniform sampler2D MotionTex;
layout(push_constant) uniform Externals { float data[32]; } u;

mat4 loadReprojection() {
  return mat4(u.data[7], u.data[8], u.data[9], u.data[10],
              u.data[11], u.data[12], u.data[13], u.data[14],
              u.data[15], u.data[16], u.data[17], u.data[18],
              u.data[19], u.data[20], u.data[21], u.data[22]);
}

vec4 cubicWeights(float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return vec4(-0.5 * t + t2 - 0.5 * t3,
              1.0 - 2.5 * t2 + 1.5 * t3,
              0.5 * t + 2.0 * t2 - 1.5 * t3,
              -0.5 * t2 + 0.5 * t3);
}

vec4 sampleHistoryCatmullRom(vec2 uv) {
  vec2 size = vec2(textureSize(DepthTex, 0));
  vec2 position = uv * size - 0.5;
  vec2 base = floor(position);
  vec2 fraction = fract(position);
  vec4 wx = cubicWeights(fraction.x);
  vec4 wy = cubicWeights(fraction.y);
  vec4 result = vec4(0.0);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x)
      result += texture(DepthTex, (base + vec2(x - 1, y - 1) + 0.5) / size) * wx[x] * wy[y];
  return result;
}

vec3 rgbToYCoCg(vec3 rgb) {
  return vec3(rgb.r * 0.25 + rgb.g * 0.5 + rgb.b * 0.25,
              rgb.r * 0.5 - rgb.b * 0.5,
             -rgb.r * 0.25 + rgb.g * 0.5 - rgb.b * 0.25);
}

vec3 yCoCgToRgb(vec3 c) {
  return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec3 encodeHdrYCoCg(vec3 rgb) {
  vec3 ycocg = rgbToYCoCg(max(rgb, vec3(0.0)));
  float range = 1.0 + max(ycocg.x, 0.0);
  return vec3(log2(range), ycocg.yz / range);
}

vec3 decodeHdrYCoCg(vec3 encoded) {
  float y = max(exp2(encoded.x) - 1.0, 0.0);
  float range = 1.0 + y;
  return max(yCoCgToRgb(vec3(y, encoded.yz * range)), vec3(0.0));
}

void main() {
  vec3 current = texture(MainTex, fragUV).rgb;
  float linearDepth = u.data[26] > 0.5 ? texture(MotionTex, fragUV).r
                                       : texture(MainTex, fragUV).a;
  if (u.data[4] < 0.5) {
    outColor = vec4(current * fragColor.rgb, linearDepth);
    return;
  }

  vec2 texel = vec2(u.data[0], u.data[1]);
  vec3 currentYCoCg = encodeHdrYCoCg(current);
  vec3 neighborhoodMin = currentYCoCg;
  vec3 neighborhoodMax = currentYCoCg;
  vec3 neighborhoodSum = vec3(0.0);
  vec3 neighborhoodSumSq = vec3(0.0);
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      vec3 sampleColor = encodeHdrYCoCg(texture(MainTex, fragUV + vec2(x, y) * texel).rgb);
      neighborhoodMin = min(neighborhoodMin, sampleColor);
      neighborhoodMax = max(neighborhoodMax, sampleColor);
      neighborhoodSum += sampleColor;
      neighborhoodSumSq += sampleColor * sampleColor;
    }
  }
  vec3 mean = neighborhoodSum / 9.0;
  vec3 sigma = sqrt(max(neighborhoodSumSq / 9.0 - mean * mean, vec3(0.0)));
  vec3 varianceMin = mean - sigma * 1.25;
  vec3 varianceMax = mean + sigma * 1.25;
  neighborhoodMin = max(neighborhoodMin, varianceMin);
  neighborhoodMax = min(neighborhoodMax, varianceMax);

  vec2 historyUV = fragUV + vec2(u.data[5], u.data[6]);
  if (u.data[25] > 0.5 && linearDepth > 1e-5 && linearDepth < 0.9999) {
    float nearZ = max(u.data[23], 1e-4);
    float farZ = max(u.data[24], nearZ + 1e-3);
    float viewDepth = mix(nearZ, farZ, linearDepth);
    float ndcZ = (farZ - nearZ * farZ / max(viewDepth, nearZ)) / (farZ - nearZ);
    vec4 previousClip = loadReprojection() * vec4(fragUV * 2.0 - 1.0, ndcZ, 1.0);
    if (abs(previousClip.w) > 1e-6)
      historyUV = previousClip.xy / previousClip.w * 0.5 + 0.5;
  }
  vec2 objectMotion = vec2(0.0);
  if (u.data[26] > 0.5 && linearDepth > 1e-5 && linearDepth < 0.9999) {
    vec4 nearestMotion = texture(MotionTex, fragUV);
    for (int y = -1; y <= 1; ++y) {
      for (int x = -1; x <= 1; ++x) {
        vec4 candidate = texture(MotionTex, fragUV + vec2(x, y) * texel);
        if (candidate.r < nearestMotion.r) nearestMotion = candidate;
      }
    }
    objectMotion = (nearestMotion.gb - 0.5) * 2.0;
    historyUV += objectMotion;
  }
  bool historyInBounds = all(greaterThanEqual(historyUV, vec2(0.0))) &&
                         all(lessThanEqual(historyUV, vec2(1.0)));
  vec4 historySample = sampleHistoryCatmullRom(clamp(historyUV, vec2(0.0), vec2(1.0)));
  vec3 history = historySample.rgb;
  vec3 historyYCoCg = encodeHdrYCoCg(history);
  vec3 clippedYCoCg = mix(historyYCoCg, clamp(historyYCoCg, neighborhoodMin, neighborhoodMax),
                          clamp(u.data[3], 0.0, 1.0));
  vec3 clippedHistory = decodeHdrYCoCg(clippedYCoCg);
  vec3 encodedDelta = abs(currentYCoCg - clippedYCoCg);
  float colorDelta = max(encodedDelta.x, max(encodedDelta.y, encodedDelta.z));
  float disocclusion = smoothstep(0.03, 0.15, colorDelta);
  float depthThreshold = 0.006 + linearDepth * 0.02;
  float depthReject = smoothstep(depthThreshold, depthThreshold * 4.0,
                                 abs(linearDepth - historySample.a));
  float velocityReactive = smoothstep(0.002, 0.04, length(objectMotion));
  float surfaceReactive = 0.0;
  if (u.data[26] > 0.5 && linearDepth > 1e-5 && linearDepth < 0.9999)
    surfaceReactive = smoothstep(0.01, 0.08,
                                 abs(linearDepth - texture(MotionTex, fragUV).r));
  float reject = max(max(disocclusion, depthReject),
                     max(velocityReactive * 0.65, surfaceReactive));
  if (!historyInBounds) reject = 1.0;
  float currentWeight = mix(clamp(u.data[2], 0.0, 1.0), 1.0, reject);
  vec3 resolved = mix(clippedHistory, current, currentWeight);
  vec3 crossAverage =
      (encodeHdrYCoCg(texture(MainTex, fragUV + vec2(texel.x, 0.0)).rgb) +
       encodeHdrYCoCg(texture(MainTex, fragUV - vec2(texel.x, 0.0)).rgb) +
       encodeHdrYCoCg(texture(MainTex, fragUV + vec2(0.0, texel.y)).rgb) +
       encodeHdrYCoCg(texture(MainTex, fragUV - vec2(0.0, texel.y)).rgb)) * 0.25;
  float sharpenStrength = 0.12 * (1.0 - reject) * (1.0 - velocityReactive);
  vec3 sharpenedYCoCg = encodeHdrYCoCg(resolved) +
                        (currentYCoCg - crossAverage) * sharpenStrength;
  resolved = decodeHdrYCoCg(clamp(sharpenedYCoCg, neighborhoodMin, neighborhoodMax));
  outColor = vec4(resolved * fragColor.rgb, linearDepth);
}
