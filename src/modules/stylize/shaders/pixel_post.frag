#version 450
// Pixel-art post (unity-isometric-pixel-pipeline / bevy_pixel_art_shader):
// low-res UV snap, silhouette 1px outline, cel bands, palette + Bayer dither.
// Push: 0 pixelSize, 1 paletteSteps, 2 ditherStrength, 3 toonBands,
//       4 sharpness, 5 texelW, 6 texelH, 7 time, 8 screenW, 9 screenH,
//       10 outline

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

float bayer4(ivec2 p) {
  int x = p.x & 3;
  int y = p.y & 3;
  int m[16] = int[](
      0,  8,  2, 10,
     12,  4, 14,  6,
      3, 11,  1,  9,
     15,  7, 13,  5);
  return (float(m[x + y * 4]) / 16.0) - 0.5;
}

float objectMask(vec3 rgb) {
  float Y = luma(rgb);
  float sat = length(rgb - vec3(Y));
  float isBg = (1.0 - smoothstep(0.10, 0.26, Y)) * (1.0 - smoothstep(0.025, 0.09, sat));
  return clamp(1.0 - isBg, 0.0, 1.0);
}

vec3 quantize(vec3 col, float steps, float dither) {
  col = clamp(col + vec3(dither), 0.0, 1.0);
  return floor(col * steps + 1e-4) / max(steps - 1.0, 1.0);
}

void main() {
  float pixelSize = max(u.data[0], 1.0);
  float paletteSteps = max(u.data[1], 2.0);
  float ditherStrength = u.data[2];
  float toonBands = max(u.data[3], 1.0);
  float sharpness = clamp(u.data[4], 0.0, 1.0);
  float screenW = max(u.data[8], 1.0);
  float screenH = max(u.data[9], 1.0);
  float outline = u.data[10];

  vec2 res = vec2(screenW, screenH) / pixelSize;
  vec2 cell = floor(fragUV * res);
  vec2 uvPix = (cell + 0.5) / res;

  // Hard pixel snap (sharpness=1 → pure nearest cell).
  vec2 uv = mix(fragUV, uvPix, sharpness);

  vec3 raw = texture(MainTex, uv).rgb * fragColor.rgb;
  float obj = objectMask(raw);
  vec3 col = raw;
  // Recover form from over-bright PBR before quantize (object only).
  if (obj > 0.05) {
    col = raw / (raw + vec3(0.7)) * 1.35;
    col = clamp(col, 0.0, 1.0);
    if (toonBands > 1.5) {
      float Y = max(luma(col), 1e-4);
      float cel = (floor(Y * toonBands) + 0.5) / toonBands;
      cel = max(cel, 0.45 / toonBands);
      col *= (cel / Y);
    }
  }

  // Silhouette outline at internal resolution only.
  if (outline > 0.01) {
    vec2 stepUV = 1.0 / res;
    float m0 = objectMask(texture(MainTex, uvPix).rgb);
    float mL = objectMask(texture(MainTex, uvPix + vec2(-stepUV.x, 0.0)).rgb);
    float mR = objectMask(texture(MainTex, uvPix + vec2(stepUV.x, 0.0)).rgb);
    float mU = objectMask(texture(MainTex, uvPix + vec2(0.0, -stepUV.y)).rgb);
    float mD = objectMask(texture(MainTex, uvPix + vec2(0.0, stepUV.y)).rgb);
    float mMin = min(min(mL, mR), min(mU, mD));
    if (m0 > 0.45 && mMin < 0.25)
      col = mix(col, vec3(0.08, 0.07, 0.1), clamp(outline, 0.0, 1.0));
  }

  ivec2 sp = ivec2(cell);
  float d = bayer4(sp) * ditherStrength / max(paletteSteps, 1.0);
  if (obj > 0.05) col = quantize(col, paletteSteps, d);

  outColor = vec4(clamp(col, 0.0, 1.0), texture(MainTex, fragUV).a * fragColor.a);
}
