#version 450
// SSAA / supersample resolve — box, tent, or Gaussian filter from a higher-res source.
// Typical use: render scene into a 2×/4× Canvas, then resolve into screen / dest.
// Push constants (declareFloat order):
//  0 texelW, 1 texelH     — source texel size (1/srcW, 1/srcH)
//  2 scale                — supersample factor (2 or 4 typical)
//  3 kernel               — 0=box 1=tent 2=gaussian
//  4 sampleRadius         — kernel radius in destination pixels (quality)

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  vec2 texel = vec2(u.data[0], u.data[1]);
  float scale = max(u.data[2], 1.0);
  int kernel = int(clamp(u.data[3], 0.0, 2.0));
  float radius = clamp(u.data[4], 0.5, 3.0);

  // Sample in source UV space; dest UV maps 1:1 onto the supersampled image.
  vec2 stepUV = texel * scale; // one dest pixel in source UV ≈ scale source texels
  int r = int(ceil(radius));
  vec3 acc = vec3(0.0);
  float wsum = 0.0;

  for (int y = -3; y <= 3; ++y) {
    if (y < -r || y > r) continue;
    for (int x = -3; x <= 3; ++x) {
      if (x < -r || x > r) continue;
      float fx = float(x);
      float fy = float(y);
      float w = 1.0;
      if (kernel == 1) {
        // Tent / bilinear-ish
        w = (1.0 - abs(fx) / (radius + 1e-4)) * (1.0 - abs(fy) / (radius + 1e-4));
        w = max(w, 0.0);
      } else if (kernel == 2) {
        float sigma = max(radius * 0.5, 0.35);
        float d2 = fx * fx + fy * fy;
        w = exp(-d2 / (2.0 * sigma * sigma));
      }
      // Box: w = 1
      // Rotate sample grid slightly for better rotational AA when scale>=2
      vec2 offset = vec2(fx, fy) * stepUV / scale;
      if (scale >= 1.5) {
        // Quincunx-ish half-pixel bias on odd taps
        offset += vec2(0.25, 0.25) * texel * float((x + y) & 1);
      }
      acc += texture(MainTex, fragUV + offset).rgb * w;
      wsum += w;
    }
  }

  vec3 rgb = acc / max(wsum, 1e-4);
  outColor = vec4(rgb * fragColor.rgb, 1.0);
}
