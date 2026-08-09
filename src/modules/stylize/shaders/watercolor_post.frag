#version 450
// Watercolor post after Bousseau / Montesdeoca / PCM2004:
// paper substrate, UV wobble, wet-in-wet bleed, edge darkening, granulation.
// Push: 0 blurAmount, 1 edgeDarken, 2 paperStrength, 3 distortion,
//       4 bleed, 5 saturation, 6 texelW, 7 texelH, 8 time, 9 granulation

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float paperHeight(vec2 uv) {
  float h = 0.0;
  float amp = 0.55;
  float freq = 14.0;
  for (int i = 0; i < 5; ++i) {
    h += valueNoise(uv * freq) * amp;
    freq *= 2.05;
    amp *= 0.48;
  }
  return clamp(h, 0.0, 1.0);
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 adjustSat(vec3 c, float s) {
  float Y = luma(c);
  return mix(vec3(Y), c, s);
}

// Flat dark desaturated plate = background. Evaluate on RAW colors only
// (tone-compress lifts empty bg and muddy-grays the paper).
float objectMask(vec3 rgb) {
  float Y = luma(rgb);
  float sat = length(rgb - vec3(Y));
  float isBg = (1.0 - smoothstep(0.10, 0.26, Y)) * (1.0 - smoothstep(0.025, 0.09, sat));
  return clamp(1.0 - isBg, 0.0, 1.0);
}

vec3 sampleBleed(vec2 uv, vec2 texel, float radius) {
  vec3 acc = texture(MainTex, uv).rgb * 0.18;
  acc += texture(MainTex, uv + vec2(radius, 0.0) * texel).rgb * 0.11;
  acc += texture(MainTex, uv + vec2(-radius, 0.0) * texel).rgb * 0.11;
  acc += texture(MainTex, uv + vec2(0.0, radius) * texel).rgb * 0.11;
  acc += texture(MainTex, uv + vec2(0.0, -radius) * texel).rgb * 0.11;
  acc += texture(MainTex, uv + vec2(radius, radius) * texel).rgb * 0.08;
  acc += texture(MainTex, uv + vec2(-radius, radius) * texel).rgb * 0.08;
  acc += texture(MainTex, uv + vec2(radius, -radius) * texel).rgb * 0.08;
  acc += texture(MainTex, uv + vec2(-radius, -radius) * texel).rgb * 0.08;
  float r2 = radius * 2.0;
  acc += texture(MainTex, uv + vec2(r2, 0.0) * texel).rgb * 0.03;
  acc += texture(MainTex, uv + vec2(-r2, 0.0) * texel).rgb * 0.03;
  return acc;
}

float colorGrad(vec2 uv, vec2 texel) {
  vec3 dx = texture(MainTex, uv + vec2(texel.x, 0.0)).rgb -
            texture(MainTex, uv - vec2(texel.x, 0.0)).rgb;
  vec3 dy = texture(MainTex, uv + vec2(0.0, texel.y)).rgb -
            texture(MainTex, uv - vec2(0.0, texel.y)).rgb;
  return (length(dx) + length(dy)) * 0.5;
}

void main() {
  float blurAmount = max(u.data[0], 0.0);
  float edgeDarken = u.data[1];
  float paperStrength = clamp(u.data[2], 0.0, 1.0);
  float distortion = u.data[3];
  float bleed = clamp(u.data[4], 0.0, 1.0);
  float saturation = u.data[5];
  vec2 texel = vec2(max(u.data[6], 1e-5), max(u.data[7], 1e-5));
  float t = u.data[8];
  float granulation = clamp(u.data[9], 0.0, 1.0);

  vec3 paperTint = vec3(0.96, 0.93, 0.86);
  vec2 pUV = fragUV * vec2(2.8, 2.1) + vec2(t * 0.008, 0.0);
  float h = paperHeight(pUV);
  float hx = paperHeight(pUV + vec2(0.012, 0.0));
  float hy = paperHeight(pUV + vec2(0.0, 0.012));
  vec2 paperGrad = vec2(hx - h, hy - h);

  // Small wobble only — large UV jumps sample the empty plate and blacken paint.
  vec2 uv = fragUV + paperGrad * distortion * 0.018;
  uv = clamp(uv, vec2(0.001), vec2(0.999));

  vec3 centerRaw = texture(MainTex, fragUV).rgb;
  vec3 sharpRaw = texture(MainTex, uv).rgb;
  // Prefer in-object samples when wobble hits the plate.
  if (objectMask(sharpRaw) < objectMask(centerRaw) * 0.6) sharpRaw = centerRaw;
  vec3 softRaw = sampleBleed(fragUV, texel, max(blurAmount, 0.8));
  float mask0 = objectMask(centerRaw);
  float mask = max(objectMask(sharpRaw), mask0);
  // Dilate mask slightly so pigment bleeds onto paper at the silhouette.
  float maskSoft = mask;
  for (int i = 0; i < 4; ++i) {
    float a = float(i) * 1.5707963;
    vec2 o = vec2(cos(a), sin(a)) * texel * (2.0 + blurAmount);
    maskSoft = max(maskSoft, objectMask(texture(MainTex, fragUV + o).rgb) * 0.85);
  }
  maskSoft = max(maskSoft, mask0);

  // Soften highlights after masking so wash keeps pigment.
  vec3 sharp = sharpRaw / (sharpRaw + vec3(0.45)) * 1.35;
  vec3 soft = softRaw / (softRaw + vec3(0.45)) * 1.35;
  vec3 paint = mix(sharp, soft, bleed * mask);
  float g = colorGrad(fragUV, texel * 1.4);
  // Bousseau edge darkening — pigment pools at discontinuities (keep subtle).
  float edgeAmt = smoothstep(0.06, 0.35, g) * clamp(edgeDarken, 0.0, 2.5) * 0.35 * mask;
  paint *= (1.0 - edgeAmt);

  float Hiv = 1.0 - ((h * 0.4) + 0.6);
  float valley = smoothstep(0.3, 0.9, Hiv);
  paint *= 1.0 - valley * granulation * 0.55 * mask;

  float peak = smoothstep(0.5, 0.95, h);
  paint = mix(paint, mix(paint, paperTint, 0.65), peak * paperStrength * 0.4 * mask);

  float fiber = mix(0.9, 1.08, h);
  paint *= mix(1.0, fiber, paperStrength * mask);
  paint = adjustSat(paint, mix(1.0, saturation, mask));
  paint = clamp(paint, 0.0, 1.0);

  // Full-frame xuan / cold-press paper plate (research demos sit on paper, not void).
  vec3 paperCol = paperTint * mix(0.93, 1.05, h);
  paperCol *= 1.0 - step(0.985, hash21(floor(fragUV * 700.0))) * 0.06;

  // Soft wash fringe into paper.
  float fringe = smoothstep(0.05, 0.75, maskSoft);
  vec3 col = mix(paperCol, paint, fringe);
  // Dry-brush scrape on peaks of painted region.
  col = mix(col, paperCol, peak * paperStrength * 0.18 * fringe);

  float a = texture(MainTex, fragUV).a * fragColor.a;
  outColor = vec4(col * fragColor.rgb, a);
}
