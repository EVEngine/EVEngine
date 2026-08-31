#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform sampler2D rawSceneSampler;

vec3 acesFitted(vec3 color) {
  color = max(color, vec3(0.0));
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((color * (a * color + b)) /
                   (color * (c * color + d) + e),
               0.0, 1.0);
}

vec3 linearToSrgb(vec3 color) {
  vec3 low = color * 12.92;
  vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
  return mix(low, high, greaterThan(color, vec3(0.0031308)));
}

float automaticExposure(sampler2D source) {
  float logLuminance[16];
  int sampleIndex = 0;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      vec2 uv = (vec2(x, y) + vec2(0.5)) * 0.25;
      vec3 sampleColor = max(texture(source, uv).rgb, vec3(0.0));
      float luminance = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
      logLuminance[sampleIndex++] = log2(clamp(luminance, 1e-4, 65504.0));
    }
  }
  for (int i = 1; i < 16; ++i) {
    float value = logLuminance[i];
    int j = i - 1;
    while (j >= 0 && logLuminance[j] > value) {
      logLuminance[j + 1] = logLuminance[j];
      --j;
    }
    logLuminance[j + 1] = value;
  }
  float trimmedLogSum = 0.0;
  for (int i = 2; i < 14; ++i)
    trimmedLogSum += logLuminance[i];
  float geometricMean = exp2(trimmedLogSum * (1.0 / 12.0));
  float packed = round(fragColor.b);
  float minEV = mod(packed, 256.0) * (1.0 / 8.0) - 16.0;
  float maxEV = floor(packed * (1.0 / 256.0)) * (1.0 / 8.0) - 16.0;
  return clamp(0.18 / max(geometricMean, 1e-4), exp2(minEV), exp2(maxEV));
}

vec3 bloomPrefilter(vec3 color, float threshold) {
  float brightness = max(color.r, max(color.g, color.b));
  float knee = max(threshold * 0.1, 0.01);
  float soft = clamp((brightness - threshold + knee) / (2.0 * knee), 0.0, 1.0);
  float contribution = max(brightness - threshold, soft * soft * knee);
  return color * (contribution / max(brightness, 1e-4));
}

vec3 sampleBloom(vec2 uv, float threshold) {
  vec2 texel = 1.0 / vec2(textureSize(texSampler, 0));
  vec3 bloom = bloomPrefilter(texture(texSampler, uv).rgb, threshold) * 0.2;
  const vec2 offsets[12] = vec2[](
      vec2(2.00, 0.00), vec2(-1.47, 1.35), vec2(0.17, -1.99), vec2(1.22, 1.58),
      vec2(4.22, -2.68), vec2(-4.92, -0.87), vec2(3.04, 3.97), vec2(-0.44, -4.98),
      vec2(-5.28, 7.28), vec2(8.89, -1.41), vec2(-7.82, -4.46), vec2(2.07, 8.76));
  const float weights[12] = float[](
      0.10, 0.10, 0.10, 0.10,
      0.06, 0.06, 0.06, 0.06,
      0.04, 0.04, 0.04, 0.04);
  for (int i = 0; i < 12; ++i)
    bloom += bloomPrefilter(texture(texSampler, uv + offsets[i] * texel).rgb,
                            threshold) * weights[i];
  return bloom;
}

void main() {
  vec4 hdr = texture(texSampler, fragUV);
  float exposure = 1.0;  // Applied once by the dedicated HDR pre-exposure pass.
  bool encodeSrgb = fragColor.a >= 65536.0;
  float bloomPacked = mod(round(fragColor.a), 65536.0);
  float bloomIntensity = mod(bloomPacked, 256.0) * (1.0 / 32.0);
  float bloomThreshold = floor(bloomPacked * (1.0 / 256.0)) * (1.0 / 16.0);
  vec3 bloom = bloomIntensity > 0.0 ? sampleBloom(fragUV, bloomThreshold) : vec3(0.0);
  vec3 displayColor = acesFitted((hdr.rgb + bloom * bloomIntensity) * exposure);
  if (encodeSrgb) displayColor = linearToSrgb(displayColor);
  outColor = vec4(displayColor, hdr.a);
}
