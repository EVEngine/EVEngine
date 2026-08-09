#version 450
// Cartoon / cel post-process: posterize + luminance bands + Sobel outline.
// Push constants (StylePass):
//   0 bands, 1 outlineStrength, 2 outlineThreshold, 3 posterize,
//   4 texelW, 5 texelH, 6 time, 7 softEdge

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals {
  float data[32];
} u;

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

void main() {
  float bands = max(u.data[0], 2.0);
  float outlineStrength = u.data[1];
  float outlineThreshold = max(u.data[2], 0.01);
  float posterize = max(u.data[3], 2.0);
  vec2 texel = vec2(max(u.data[4], 1e-5), max(u.data[5], 1e-5));
  float softEdge = max(u.data[7], 0.001);

  vec4 src = texture(MainTex, fragUV) * fragColor;
  vec3 col = src.rgb;

  // Soft cel bands on luminance, preserve hue.
  float Y = luma(col);
  float cel = floor(Y * bands) / max(bands - 1.0, 1.0);
  float scale = (Y > 1e-4) ? (cel / Y) : 1.0;
  col *= scale;

  // Posterize toward flat painted colors.
  col = floor(col * posterize + 0.5) / posterize;

  float edge = sobel(fragUV, texel);
  float line = smoothstep(outlineThreshold, outlineThreshold + softEdge, edge);
  col = mix(col, vec3(0.05, 0.04, 0.08), clamp(line * outlineStrength, 0.0, 1.0));

  outColor = vec4(col, src.a);
}
