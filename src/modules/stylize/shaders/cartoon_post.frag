#version 450
// Cel / toon post (UTS / ToonForge-inspired image-space path):
// luminance cel bands + soft HSV posterize + silhouette-only outline.
// Internal albedo edges (stripes) are ignored so outlines stay on the form.
// Push: 0 bands, 1 outlineStrength, 2 outlineThreshold, 3 posterize,
//       4 texelW, 5 texelH, 6 time, 7 softEdge, 8 outlineWidth, 9 shadowLift

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 rgb2hsv(vec3 c) {
  float M = max(c.r, max(c.g, c.b));
  float m = min(c.r, min(c.g, c.b));
  float d = M - m;
  float h = 0.0;
  if (d > 1e-5) {
    if (M == c.r) h = mod((c.g - c.b) / d, 6.0);
    else if (M == c.g) h = (c.b - c.r) / d + 2.0;
    else h = (c.r - c.g) / d + 4.0;
    h /= 6.0;
    if (h < 0.0) h += 1.0;
  }
  float s = (M <= 1e-5) ? 0.0 : d / M;
  return vec3(h, s, M);
}

vec3 hsv2rgb(vec3 c) {
  float h = c.x * 6.0;
  float s = c.y;
  float v = c.z;
  float f = fract(h);
  float p = v * (1.0 - s);
  float q = v * (1.0 - s * f);
  float t = v * (1.0 - s * (1.0 - f));
  if (h < 1.0) return vec3(v, t, p);
  if (h < 2.0) return vec3(q, v, p);
  if (h < 3.0) return vec3(p, v, t);
  if (h < 4.0) return vec3(p, q, v);
  if (h < 5.0) return vec3(t, p, v);
  return vec3(v, p, q);
}

// Flat dark desaturated plate = background; chromatic / lit pixels = object.
float objectMask(vec3 rgb) {
  float Y = luma(rgb);
  float sat = length(rgb - vec3(Y));
  float isBg = (1.0 - smoothstep(0.10, 0.26, Y)) * (1.0 - smoothstep(0.025, 0.09, sat));
  return clamp(1.0 - isBg, 0.0, 1.0);
}

float silhouetteEdge(vec2 uv, vec2 texel, float width) {
  float m0 = objectMask(texture(MainTex, uv).rgb);
  float mMin = m0;
  int w = int(clamp(width, 1.0, 3.0));
  for (int y = -w; y <= w; ++y) {
    for (int x = -w; x <= w; ++x) {
      if (x == 0 && y == 0) continue;
      float m = objectMask(texture(MainTex, uv + vec2(float(x), float(y)) * texel).rgb);
      mMin = min(mMin, m);
    }
  }
  // Object pixel next to background → ink line (ignores stripe chroma edges).
  return smoothstep(0.15, 0.55, m0) * (1.0 - smoothstep(0.05, 0.45, mMin));
}

void main() {
  float bands = max(u.data[0], 2.0);
  float outlineStrength = u.data[1];
  float outlineThreshold = max(u.data[2], 0.01);
  float posterize = max(u.data[3], 2.0);
  vec2 texel = vec2(max(u.data[4], 1e-5), max(u.data[5], 1e-5));
  float softEdge = max(u.data[7], 0.001);
  float outlineWidth = max(u.data[8], 1.0);
  float shadowLift = u.data[9];

  vec4 src = texture(MainTex, fragUV) * fragColor;
  vec3 col = clamp(src.rgb, 0.0, 1.0);
  float obj = objectMask(src.rgb);

  // Only cel-shade the object; leave the plate alone (no purple posterize fog).
  if (obj > 0.05) {
    // Mild tone compress so blown highlights still take cel steps.
    vec3 mapped = col / (col + vec3(0.55)) * 1.45;
    mapped = clamp(mapped, 0.0, 1.0);

    vec3 hsv = rgb2hsv(mapped);
    float vBand = floor(hsv.z * bands + 1e-4) / max(bands - 1.0, 1.0);
    vBand = max(vBand, 0.5 / bands);
    // Keep a ceiling so the brightest cel step is not pure white.
    vBand = min(vBand, 0.88);
    float bandT = vBand;
    hsv.y = clamp(hsv.y * mix(1.3, 1.05, bandT), 0.0, 1.0);
    hsv.z = mix(vBand * (0.7 + shadowLift), vBand, bandT);
    col = hsv2rgb(hsv);

    hsv = rgb2hsv(col);
    hsv.z = min(floor(hsv.z * posterize + 0.5) / posterize, 0.9);
    hsv.y = max(floor(hsv.y * max(posterize * 0.7, 2.0) + 0.5) / max(posterize * 0.7, 2.0),
                hsv.y * 0.85);
    col = hsv2rgb(hsv);
    col = mix(src.rgb, col, clamp(obj, 0.0, 1.0));
  }

  float edge = silhouetteEdge(fragUV, texel, outlineWidth);
  float line = smoothstep(outlineThreshold, outlineThreshold + softEdge, edge);
  line = clamp(line * outlineStrength, 0.0, 1.0);
  col = mix(col, vec3(0.05, 0.04, 0.07), line * obj);

  outColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
