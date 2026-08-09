#version 450
// Chinese / East-Asian ink-wash (水墨) post-process:
// tone quantization, silhouette edges, ink diffusion, xuan-paper tint.
// Refs: Park et al. Sumi-e; Okami-style ink NPR; view/luma silhouette.
// Push constants:
//   0 inkContrast, 1 washLevels, 2 edgeThreshold, 3 diffusion,
//   4 paperR, 5 paperG, 6 paperB, 7 inkDensity,
//   8 texelW, 9 texelH, 10 time, 11 edgeStrength

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals {
  float data[32];
} u;

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
  float inkContrast = max(u.data[0], 0.1);
  float washLevels = max(u.data[1], 2.0);
  float edgeThreshold = max(u.data[2], 0.01);
  float diffusion = max(u.data[3], 0.0);
  vec3 paper = vec3(u.data[4], u.data[5], u.data[6]);
  float inkDensity = u.data[7];
  vec2 texel = vec2(max(u.data[8], 1e-5), max(u.data[9], 1e-5));
  float t = u.data[10];
  float edgeStrength = u.data[11];

  // Mild UV jitter for brush irregularity.
  float n = noise(fragUV * 40.0 + t * 0.2);
  vec2 uv = fragUV + (n - 0.5) * texel * 2.0;

  // Ink diffusion: blurred luma raised by contrast (sumi-e pulp effect).
  float Y = luma(texture(MainTex, uv).rgb);
  float blur = 0.0;
  float o = diffusion;
  blur += luma(texture(MainTex, uv + vec2(o, 0.0) * texel).rgb);
  blur += luma(texture(MainTex, uv + vec2(-o, 0.0) * texel).rgb);
  blur += luma(texture(MainTex, uv + vec2(0.0, o) * texel).rgb);
  blur += luma(texture(MainTex, uv + vec2(0.0, -o) * texel).rgb);
  blur += luma(texture(MainTex, uv + vec2(o, o) * texel).rgb);
  blur += luma(texture(MainTex, uv + vec2(-o, -o) * texel).rgb);
  blur = mix(Y, blur / 6.0, clamp(diffusion * 0.15, 0.0, 1.0));

  float tone = pow(clamp(blur, 0.0, 1.0), inkContrast);
  tone = floor(tone * washLevels + 1e-4) / max(washLevels - 1.0, 1.0);

  // Silhouette / contour ink.
  float edge = sobel(uv, texel);
  float contour = smoothstep(edgeThreshold, edgeThreshold + 0.15, edge);
  // Second softer contour pass for brush thickness.
  float contour2 = smoothstep(edgeThreshold * 0.5, edgeThreshold * 0.5 + 0.25, edge);
  float inkLine = max(contour, contour2 * 0.55) * edgeStrength;

  // Dense ink where dark + edges; sparse wash elsewhere.
  float ink = (1.0 - tone) * inkDensity + inkLine;
  ink = clamp(ink + (noise(fragUV * 120.0) - 0.5) * 0.08, 0.0, 1.0);

  // Xuan paper fiber.
  float fiber = mix(0.92, 1.05, noise(fragUV * 160.0));
  vec3 paperCol = paper * fiber;
  vec3 inkCol = vec3(0.05, 0.05, 0.07);
  vec3 col = mix(paperCol, inkCol, clamp(ink, 0.0, 1.0));

  float a = texture(MainTex, fragUV).a * fragColor.a;
  outColor = vec4(col * fragColor.rgb, a);
}
