#version 450
// Temporal SSR resolve. MainTex = current premultiplied reflection,
// DepthTex = previous resolved reflection, MotionTex = linear depth + motion.
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
layout(binding = 2) uniform sampler2D MotionTex;
layout(push_constant) uniform Externals { float data[32]; } u;

mat4 reprojection() {
  return mat4(u.data[0], u.data[1], u.data[2], u.data[3],
              u.data[4], u.data[5], u.data[6], u.data[7],
              u.data[8], u.data[9], u.data[10], u.data[11],
              u.data[12], u.data[13], u.data[14], u.data[15]);
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

vec4 temporalDecodeInput(vec4 sampleValue) {
  if (u.data[23] > 0.5)
    sampleValue.rgb *= sampleValue.a;
  return sampleValue;
}

vec4 temporalEncodeOutput(vec4 premultiplied) {
  if (u.data[23] > 0.5)
    premultiplied.rgb /= max(premultiplied.a, 1e-4);
  return premultiplied;
}

vec4 spatialCurrent(vec2 uv) {
  vec2 baseTexel = vec2(u.data[16], u.data[17]);
  vec2 texel = baseTexel * max(u.data[24], 1.0);
  float centerDepth = texture(MotionTex, uv).r;
  vec4 center = temporalDecodeInput(texture(MainTex, uv));
  vec3 centerRadiance = center.rgb / max(center.a, 0.05);
  float centerLogLuma = log2(1.0 + dot(max(centerRadiance, vec3(0.0)),
                                      vec3(0.2126, 0.7152, 0.0722)));
  float depthGradient = max(
      max(abs(texture(MotionTex, uv + vec2(baseTexel.x, 0.0)).r - centerDepth),
          abs(texture(MotionTex, uv - vec2(baseTexel.x, 0.0)).r - centerDepth)),
      max(abs(texture(MotionTex, uv + vec2(0.0, baseTexel.y)).r - centerDepth),
          abs(texture(MotionTex, uv - vec2(0.0, baseTexel.y)).r - centerDepth)));
  float depthScale = max(0.0015, depthGradient * 2.0 + 0.0005);
  vec4 sum = center * 4.0;
  float total = 4.0;
  const vec2 offsets[8] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0),
                                  vec2(0.0, 1.0), vec2(0.0, -1.0),
                                  vec2(1.0, 1.0), vec2(-1.0, 1.0),
                                  vec2(1.0, -1.0), vec2(-1.0, -1.0));
  for (int i = 0; i < 8; ++i) {
    vec2 sampleUV = uv + offsets[i] * texel;
    vec4 sampleValue = temporalDecodeInput(texture(MainTex, sampleUV));
    float sampleDepth = texture(MotionTex, sampleUV).r;
    float depthWeight = exp(-abs(sampleDepth - centerDepth) / depthScale);
    vec3 sampleRadiance = sampleValue.rgb / max(sampleValue.a, 0.05);
    float sampleLogLuma = log2(1.0 + dot(max(sampleRadiance, vec3(0.0)),
                                         vec3(0.2126, 0.7152, 0.0722)));
    float radianceWeight = exp(-abs(sampleLogLuma - centerLogLuma) * 1.5);
    float confidenceWeight = mix(0.35, 1.0, clamp(sampleValue.a * 6.0, 0.0, 1.0));
    float kernelWeight = i < 4 ? 2.0 : 1.0;
    float weight = kernelWeight * depthWeight * radianceWeight * confidenceWeight *
                   clamp(u.data[22], 0.0, 1.0);
    sum += sampleValue * weight;
    total += weight;
  }
  return sum / max(total, 1e-4);
}

vec3 rgbToYCoCg(vec3 rgb) {
  return vec3(rgb.r * 0.25 + rgb.g * 0.5 + rgb.b * 0.25,
              rgb.r * 0.5 - rgb.b * 0.5,
             -rgb.r * 0.25 + rgb.g * 0.5 - rgb.b * 0.25);
}

vec3 yCoCgToRgb(vec3 c) {
  return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec3 encodeHdr(vec3 rgb) {
  vec3 ycocg = rgbToYCoCg(max(rgb, vec3(0.0)));
  float range = 1.0 + max(ycocg.x, 0.0);
  return vec3(log2(range), ycocg.yz / range);
}

vec3 decodeHdr(vec3 encoded) {
  float y = max(exp2(encoded.x) - 1.0, 0.0);
  float range = 1.0 + y;
  return max(yCoCgToRgb(vec3(y, encoded.yz * range)), vec3(0.0));
}

void main() {
  vec4 current = spatialCurrent(fragUV);
  if (u.data[18] < 0.5) {
    outColor = temporalEncodeOutput(current) * fragColor;
    return;
  }
  vec2 texel = vec2(u.data[16], u.data[17]);
  vec3 currentEncoded = encodeHdr(current.rgb);
  vec3 lo = currentEncoded;
  vec3 hi = currentEncoded;
  vec3 sum = vec3(0.0);
  vec3 sumSq = vec3(0.0);
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      vec3 s = encodeHdr(temporalDecodeInput(
          texture(MainTex, fragUV + vec2(x, y) * texel)).rgb);
      lo = min(lo, s);
      hi = max(hi, s);
      sum += s;
      sumSq += s * s;
    }
  vec3 mean = sum / 9.0;
  vec3 sigma = sqrt(max(sumSq / 9.0 - mean * mean, vec3(0.0)));
  lo = max(lo, mean - sigma * 1.5);
  hi = min(hi, mean + sigma * 1.5);
  // Limit isolated current-frame radiance to roughly three stops above the
  // compressed neighborhood mean. This preserves broad highlights while
  // preventing a one-pixel firefly from entering temporal history.
  hi.x = min(hi.x, mean.x + 3.0);
  current.rgb = decodeHdr(clamp(currentEncoded, lo, hi));

  vec4 motionDepth = texture(MotionTex, fragUV);
  float nearZ = max(u.data[19], 1e-4);
  float farZ = max(u.data[20], nearZ + 1e-3);
  float viewDepth = mix(nearZ, farZ, motionDepth.r);
  float ndcZ = (farZ - nearZ * farZ / max(viewDepth, nearZ)) / (farZ - nearZ);
  vec4 previousClip = reprojection() * vec4(fragUV * 2.0 - 1.0, ndcZ, 1.0);
  vec2 historyUV = previousClip.xy / max(abs(previousClip.w), 1e-6) * 0.5 + 0.5;
  historyUV += (motionDepth.gb - 0.5) * 2.0;
  bool valid = all(greaterThanEqual(historyUV, vec2(0.0))) &&
               all(lessThanEqual(historyUV, vec2(1.0))) && previousClip.w > 0.0;
  vec4 history = temporalDecodeInput(
      sampleHistoryCatmullRom(clamp(historyUV, vec2(0.0), vec2(1.0))));
  history.rgb = decodeHdr(clamp(encodeHdr(history.rgb), lo, hi));
  history.a = clamp(history.a, 0.0, 1.0);
  float confidenceDelta = abs(current.a - history.a);
  float velocity = length((motionDepth.gb - 0.5) * 2.0);
  float historyDepth = texture(MotionTex, clamp(historyUV, vec2(0.0), vec2(1.0))).r;
  float depthGradient = max(
      max(abs(texture(MotionTex, fragUV + vec2(texel.x, 0.0)).r - motionDepth.r),
          abs(texture(MotionTex, fragUV - vec2(texel.x, 0.0)).r - motionDepth.r)),
      max(abs(texture(MotionTex, fragUV + vec2(0.0, texel.y)).r - motionDepth.r),
          abs(texture(MotionTex, fragUV - vec2(0.0, texel.y)).r - motionDepth.r)));
  float depthTolerance = max(0.0015, depthGradient * 2.0 + velocity * 0.5);
  float depthReject = smoothstep(depthTolerance, depthTolerance * 4.0,
                                 abs(historyDepth - motionDepth.r));
  float reject = max(depthReject,
                     max(smoothstep(0.08, 0.35, confidenceDelta),
                         smoothstep(0.01, 0.08, velocity)));
  if (!valid || current.a < 0.01) reject = 1.0;
  float currentWeight = mix(clamp(u.data[21], 0.02, 1.0), 1.0, reject);
  outColor = temporalEncodeOutput(mix(history, current, currentWeight)) * fragColor;
}
