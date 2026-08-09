#version 450
// Hand-painted watercolor post-process (image-space NPR):
// paper UV wobble, soft bleed blur, edge darkening, granulation noise.
// Refs: Bousseau et al. watercolor; Montesdeoca edge/substrate effects.
// Push constants:
//   0 blurAmount, 1 edgeDarken, 2 paperStrength, 3 distortion,
//   4 bleed, 5 saturation, 6 texelW, 7 texelH, 8 time, 9 granulation

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals {
  float data[32];
} u;

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float luma(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

float sobel(vec2 uv, vec2 texel) {
  float tl = luma(texture(MainTex, uv + vec2(-texel.x, -texel.y)).rgb);
  float t  = luma(texture(MainTex, uv + vec2(0.0, -texel.y)).rgb);
  float tr = luma(texture(MainTex, uv + vec2(texel.x, -texel.y)).rgb);
  float l  = luma(texture(MainTex, uv + vec2(-texel.x, 0.0)).rgb);
  float r  = luma(texture(MainTex, uv + vec2(texel.x, 0.0)).rgb);
  float bl = luma(texture(MainTex, uv + vec2(-texel.x, texel.y)).rgb);
  float b  = luma(texture(MainTex, uv + vec2(0.0, texel.y)).rgb);
  float br = luma(texture(MainTex, uv + vec2(texel.x, texel.y)).rgb);
  float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
  float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;
  return sqrt(gx * gx + gy * gy);
}

vec3 adjustSat(vec3 c, float s) {
  float Y = luma(c);
  return mix(vec3(Y), c, s);
}

void main() {
  float blurAmount = max(u.data[0], 0.0);
  float edgeDarken = u.data[1];
  float paperStrength = u.data[2];
  float distortion = u.data[3];
  float bleed = u.data[4];
  float saturation = u.data[5];
  vec2 texel = vec2(max(u.data[6], 1e-5), max(u.data[7], 1e-5));
  float t = u.data[8];
  float granulation = u.data[9];

  // Paper-fiber wobble of UVs.
  vec2 nUV = fragUV * 18.0 + vec2(t * 0.05, -t * 0.03);
  float n0 = noise(nUV);
  float n1 = noise(nUV * 2.3 + 17.0);
  vec2 warp = vec2(n0, n1) - 0.5;
  vec2 uv = fragUV + warp * distortion * texel * 40.0;

  // Soft pigment bleed (small kernel blur).
  float offset = blurAmount * 1.5;
  vec3 acc = texture(MainTex, uv).rgb;
  acc += texture(MainTex, uv + vec2(offset, 0.0) * texel).rgb;
  acc += texture(MainTex, uv + vec2(-offset, 0.0) * texel).rgb;
  acc += texture(MainTex, uv + vec2(0.0, offset) * texel).rgb;
  acc += texture(MainTex, uv + vec2(0.0, -offset) * texel).rgb;
  acc += texture(MainTex, uv + vec2(offset, offset) * texel).rgb;
  acc += texture(MainTex, uv + vec2(-offset, offset) * texel).rgb;
  acc += texture(MainTex, uv + vec2(offset, -offset) * texel).rgb;
  acc += texture(MainTex, uv + vec2(-offset, -offset) * texel).rgb;
  acc /= 9.0;

  vec3 sharp = texture(MainTex, uv).rgb;
  vec3 col = mix(sharp, acc, clamp(bleed, 0.0, 1.0));

  // Edge darkening (pigment gathers at stroke borders).
  float edge = sobel(uv, texel);
  col *= 1.0 - clamp(edge * edgeDarken, 0.0, 0.85);

  // Substrate granulation / paper grain.
  float paper = noise(fragUV * 90.0);
  float grit = noise(fragUV * 220.0 + 3.1);
  float grain = mix(0.85, 1.15, paper) * mix(1.0, mix(0.9, 1.1, grit), granulation);
  col *= mix(1.0, grain, clamp(paperStrength, 0.0, 1.0));

  // Slight warm paper lift in bright areas.
  vec3 paperTint = vec3(0.96, 0.93, 0.86);
  float lift = smoothstep(0.55, 1.0, luma(col));
  col = mix(col, col * paperTint, lift * 0.35 * paperStrength);

  col = adjustSat(col, saturation);
  float a = texture(MainTex, fragUV).a * fragColor.a;
  outColor = vec4(col * fragColor.rgb, a);
}
