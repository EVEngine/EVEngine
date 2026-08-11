#version 450
// Bilateral blur for AO maps packed as RGB=AO, A=depth01.
// Single-pass cross (or small box) with depth weights.
//
// Push:
//  0 texelW  1 texelH  2 depthSigma  3 spatialKernel (pixels)

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  float texelW = max(u.data[0], 1e-5);
  float texelH = max(u.data[1], 1e-5);
  float depthSigma = max(u.data[2], 1e-4);
  float kernel = clamp(u.data[3], 1.0, 4.0);

  vec4 center = texture(MainTex, fragUV);
  float cAo = center.r;
  float cD = center.a;

  float sum = cAo;
  float wsum = 1.0;

  // 3x3 + extended cross for a soft bilateral.
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      if (x == 0 && y == 0) continue;
      float dist = length(vec2(float(x), float(y)));
      if (dist > kernel + 0.1) continue;
      vec2 uv = fragUV + vec2(float(x) * texelW, float(y) * texelH);
      vec4 s = texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0)));
      float dz = abs(s.a - cD);
      float wSpatial = exp(-dist * dist / (2.0 * kernel * kernel));
      float wDepth = exp(-(dz * dz) / (2.0 * depthSigma * depthSigma));
      float w = wSpatial * wDepth;
      sum += s.r * w;
      wsum += w;
    }
  }

  float ao = sum / max(wsum, 1e-5);
  outColor = vec4(ao, ao, ao, cD) * fragColor;
}
