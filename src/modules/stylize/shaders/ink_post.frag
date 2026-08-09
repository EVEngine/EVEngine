#version 450
// Sumi-e / 水墨 post (Park et al. + Okami-style image path):
// object mask → soft tonal wash → brush silhouette → xuan paper composite.
// Push: 0 inkContrast, 1 washLevels, 2 edgeThreshold, 3 diffusion,
//       4 paperR, 5 paperG, 6 paperB, 7 inkDensity,
//       8 texelW, 9 texelH, 10 time, 11 edgeStrength

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 5; ++i) {
    v += noise(p) * a;
    p = p * 2.05 + vec2(17.0, 9.0);
    a *= 0.5;
  }
  return v;
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

float objectMaskAt(vec2 uv) {
  vec3 rgb = texture(MainTex, uv).rgb;
  float Y = luma(rgb);
  float sat = length(rgb - vec3(Y));
  float isBg = (1.0 - smoothstep(0.10, 0.26, Y)) * (1.0 - smoothstep(0.025, 0.09, sat));
  return clamp(1.0 - isBg, 0.0, 1.0);
}

float sampleLumaBlur(vec2 uv, vec2 texel, float radius) {
  float acc = luma(texture(MainTex, uv).rgb) * 0.22;
  acc += luma(texture(MainTex, uv + vec2(radius, 0.0) * texel).rgb) * 0.13;
  acc += luma(texture(MainTex, uv + vec2(-radius, 0.0) * texel).rgb) * 0.13;
  acc += luma(texture(MainTex, uv + vec2(0.0, radius) * texel).rgb) * 0.13;
  acc += luma(texture(MainTex, uv + vec2(0.0, -radius) * texel).rgb) * 0.13;
  acc += luma(texture(MainTex, uv + vec2(radius, radius) * texel).rgb) * 0.065;
  acc += luma(texture(MainTex, uv + vec2(-radius, radius) * texel).rgb) * 0.065;
  acc += luma(texture(MainTex, uv + vec2(radius, -radius) * texel).rgb) * 0.065;
  acc += luma(texture(MainTex, uv + vec2(-radius, -radius) * texel).rgb) * 0.065;
  return acc;
}

float silhouetteBrush(vec2 uv, vec2 texel, float width) {
  float m0 = objectMaskAt(uv);
  float mMin = m0;
  int w = int(clamp(width, 1.0, 3.0));
  for (int y = -w; y <= w; ++y) {
    for (int x = -w; x <= w; ++x) {
      if (x == 0 && y == 0) continue;
      mMin = min(mMin, objectMaskAt(uv + vec2(float(x), float(y)) * texel));
    }
  }
  return smoothstep(0.2, 0.65, m0) * (1.0 - smoothstep(0.05, 0.4, mMin));
}

void main() {
  float inkContrast = max(u.data[0], 0.1);
  float washLevels = max(u.data[1], 2.0);
  float edgeThreshold = max(u.data[2], 0.01);
  float diffusion = max(u.data[3], 0.5);
  vec3 paper = vec3(u.data[4], u.data[5], u.data[6]);
  float inkDensity = u.data[7];
  vec2 texel = vec2(max(u.data[8], 1e-5), max(u.data[9], 1e-5));
  float t = u.data[10];
  float edgeStrength = u.data[11];

  float tremor = fbm(fragUV * 28.0 + t * 0.12);
  vec2 uv = fragUV + (tremor - 0.5) * texel * 2.5;

  vec3 srcRaw = texture(MainTex, uv).rgb;
  float objectMask = objectMaskAt(uv);
  objectMask = max(objectMask, objectMaskAt(fragUV));
  // Compress highlights so wash keeps mid-tones (after mask from raw colors).
  vec3 src = srcRaw / (srcRaw + vec3(0.4)) * 1.3;
  float Y = luma(src);
  float Yb = sampleLumaBlur(uv, texel, diffusion);
  // Avoid lifting empty plate into the wash via blurred luma.
  Yb = mix(luma(srcRaw), Yb, objectMask);

  float pulp = pow(clamp(Yb, 0.0, 1.0), inkContrast);

  // Soft wash then light banding (sumi-e layers, not hard cel).
  float washSoft = pow(clamp(1.0 - pulp, 0.0, 1.0), 0.9);
  float washBand = floor(washSoft * washLevels + 1e-4) / max(washLevels - 1.0, 1.0);
  float wash = mix(washSoft, washBand, 0.55);

  // Silhouette brush only — ignore internal stripe Sobel edges.
  float sil = silhouetteBrush(uv, texel, 2.0);
  float brushNoise = mix(0.7, 1.2, fbm(fragUV * 110.0 + 4.0));
  // Broken dry-brush along the contour.
  float dryBreak = smoothstep(0.35, 0.75, fbm(fragUV * 160.0 + t));
  float inkLine = smoothstep(edgeThreshold * 0.4, edgeThreshold + 0.15, sil)
                  * edgeStrength * brushNoise * dryBreak;

  float fiber = fbm(fragUV * 120.0);
  vec3 paperCol = paper * mix(0.94, 1.06, fiber);
  paperCol *= 1.0 - step(0.975, hash21(floor(fragUV * 850.0))) * 0.07;

  vec3 inkCol = vec3(0.07, 0.07, 0.08);
  vec3 lightWash = mix(paperCol, inkCol, 0.22);
  vec3 midWash = mix(paperCol, inkCol, 0.48);
  vec3 deepWash = mix(paperCol, inkCol, 0.78);
  vec3 tone = mix(lightWash, midWash, smoothstep(0.15, 0.55, wash));
  tone = mix(tone, deepWash, smoothstep(0.55, 0.95, wash));

  // Slight cool/warm split in washes.
  tone *= mix(vec3(0.96, 0.97, 1.02), vec3(1.02, 0.99, 0.95), wash);

  float inkBody = objectMask * wash * inkDensity * mix(0.85, 1.1, fiber);
  vec3 col = mix(paperCol, tone, clamp(objectMask * 0.92, 0.0, 1.0));
  col = mix(col, inkCol, clamp(inkBody * 0.55, 0.0, 1.0));
  col = mix(col, inkCol, clamp(inkLine, 0.0, 1.0));

  // Feathery bleed at the silhouette into paper.
  float halo = silhouetteBrush(uv, texel, 3.0) * 0.25 * (1.0 - dryBreak);
  col = mix(col, mix(paperCol, inkCol, 0.35), halo * objectMask);

  float a = texture(MainTex, fragUV).a * fragColor.a;
  outColor = vec4(col * fragColor.rgb, a);
}
