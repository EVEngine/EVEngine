#version 450
// Pixel-art post-process: low-res UV snap, palette quantization, Bayer dither.
// Refs: unity-isometric-pixel-pipeline; bevy_pixel_art_shader.
// Push constants:
//   0 pixelSize, 1 paletteSteps, 2 ditherStrength, 3 toonBands,
//   4 sharpness, 5 texelW, 6 texelH, 7 time, 8 screenW, 9 screenH

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

// 4x4 Bayer matrix mapped to [-0.5, 0.5]
float bayer4(ivec2 p) {
  int x = p.x & 3;
  int y = p.y & 3;
  int index = x + y * 4;
  // Values 0..15
  int m[16] = int[](
      0,  8,  2, 10,
     12,  4, 14,  6,
      3, 11,  1,  9,
     15,  7, 13,  5);
  return (float(m[index]) / 16.0) - 0.5;
}

void main() {
  float pixelSize = max(u.data[0], 1.0);
  float paletteSteps = max(u.data[1], 2.0);
  float ditherStrength = u.data[2];
  float toonBands = max(u.data[3], 1.0);
  float sharpness = clamp(u.data[4], 0.0, 1.0);
  float screenW = max(u.data[8], 1.0);
  float screenH = max(u.data[9], 1.0);

  // Snap UV to a virtual low-resolution grid.
  vec2 res = vec2(screenW, screenH) / pixelSize;
  vec2 uvPix = (floor(fragUV * res) + 0.5) / res;

  // Optional sharp-upscale blend at texel boundaries (fwidth-like soft AA).
  vec2 uvCont = fragUV;
  vec2 f = fract(fragUV * res);
  float edge = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
  float aa = smoothstep(0.0, 0.15, edge);
  vec2 uv = mix(uvCont, uvPix, mix(1.0, aa, 1.0 - sharpness));
  // Prefer hard pixels when sharpness high.
  uv = mix(uvPix, uv, 1.0 - sharpness);

  vec4 src = texture(MainTex, uv) * fragColor;
  vec3 col = src.rgb;

  // Optional luminance banding before palette quantize.
  if (toonBands > 1.5) {
    float Y = luma(col);
    float cel = floor(Y * toonBands) / max(toonBands - 1.0, 1.0);
    float scale = (Y > 1e-4) ? (cel / Y) : 1.0;
    col *= scale;
  }

  // Screen-space Bayer dither then quantize (ordered dither).
  ivec2 sp = ivec2(floor(fragUV * vec2(screenW, screenH)));
  float d = bayer4(sp) * ditherStrength / max(paletteSteps, 1.0);
  col = clamp(col + vec3(d), 0.0, 1.0);
  col = floor(col * paletteSteps + 1e-4) / max(paletteSteps - 1.0, 1.0);

  outColor = vec4(col, src.a);
}
