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

vec3 rec709ToRec2020(vec3 color) {
  const mat3 m = mat3(
      0.6274040, 0.0690970, 0.0163916,
      0.3292820, 0.9195400, 0.0880132,
      0.0433136, 0.0113612, 0.8955950);
  return m * color;
}

float linearToPq(float v) {
  v = max(v, 0.0);
  const float m1 = 2610.0 / 4096.0 / 4.0;
  const float m2 = 2523.0 / 4096.0 * 128.0;
  const float c1 = 3424.0 / 4096.0;
  const float c2 = 2413.0 / 4096.0 * 32.0;
  const float c3 = 2392.0 / 4096.0 * 32.0;
  float cp = pow(v, m1);
  return pow((c1 + c2 * cp) / (1.0 + c3 * cp), m2);
}

vec3 encodeHdr10(vec3 displayLinear, float paperWhiteNits) {
  displayLinear = max(displayLinear, vec3(0.0));
  displayLinear = rec709ToRec2020(displayLinear);
  const float st2084Max = 10000.0;
  vec3 normalized = displayLinear * (paperWhiteNits / st2084Max);
  return vec3(linearToPq(normalized.r), linearToPq(normalized.g),
              linearToPq(normalized.b));
}

vec3 acesToDisplayLinear(vec3 color, float peakRatio) {
  float inv = 1.0 / max(peakRatio, 1e-3);
  return acesFitted(color * inv) * peakRatio;
}

void unpackNits(float packed, out float paperWhiteNits, out float peakNits) {
  float rounded = round(packed);
  paperWhiteNits = clamp(mod(rounded, 65536.0) * 0.1, 80.0, 400.0);
  peakNits = clamp(floor(rounded / 65536.0) * 10.0, max(paperWhiteNits, 200.0), 10000.0);
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
  int displayMode = int(round(fragColor.g));  // 0=SDR, 1=scRGB, 2=HDR10
  float paperWhiteNits = 200.0;
  float peakNits = 1000.0;
  unpackNits(fragColor.b, paperWhiteNits, peakNits);
  float peakRatio = peakNits / max(paperWhiteNits, 1.0);

  // UI resolve path: SDR overlay already in display-referred 0-1 (paper white).
  if (fragColor.r < 0.0) {
    vec3 ui = max(hdr.rgb, vec3(0.0));
    if (displayMode == 2) ui = encodeHdr10(ui, paperWhiteNits);
    else if (displayMode == 1) ui = clamp(ui, vec3(0.0), vec3(peakRatio));
    outColor = vec4(ui, hdr.a);
    return;
  }

  vec3 bloom = bloomIntensity > 0.0 ? sampleBloom(fragUV, bloomThreshold) : vec3(0.0);
  vec3 linearColor = (hdr.rgb + bloom * bloomIntensity) * exposure;
  vec3 displayColor;
  if (displayMode == 0) {
    displayColor = fragColor.r >= 0.5 ? acesFitted(linearColor)
                                     : clamp(linearColor, 0.0, 1.0);
    if (encodeSrgb) displayColor = linearToSrgb(displayColor);
  } else {
    displayColor = fragColor.r >= 0.5 ? acesToDisplayLinear(linearColor, peakRatio)
                                     : clamp(linearColor, vec3(0.0), vec3(peakRatio));
    if (displayMode == 2) displayColor = encodeHdr10(displayColor, paperWhiteNits);
  }
  outColor = vec4(displayColor, hdr.a);
}
