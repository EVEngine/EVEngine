#version 450
// Stylized cel / toon fragment for Mesh3D (UTS-like three-tone ramp).
// Push constants: data[0]=bands, data[1]=rimPower,
//   data[2]=rimStrength, data[3]=posterizeSteps

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(push_constant) uniform Externals {
  float data[32];
} u;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 base = texture(albedo, vUV) * vTint;
  vec3 N = normalize(vNormal);
  vec3 L = normalize(vLightDir);
  float ndotl = max(dot(N, L), 0.0);

  float bands = max(u.data[0], 2.0);
  float rimPower = max(u.data[1], 0.5);
  float rimStrength = u.data[2];
  float posterize = max(u.data[3], 2.0);

  // Hard cel steps with a small soft fence between bands (UTS-like).
  float celRaw = ndotl * bands;
  float cel = floor(celRaw + 1e-4) / max(bands - 1.0, 1.0);
  float fence = smoothstep(0.0, 0.12, fract(celRaw));
  cel = mix(cel, min(cel + 1.0 / max(bands - 1.0, 1.0), 1.0), fence * 0.15);

  vec3 shadowCol = vec3(0.42, 0.38, 0.58);
  vec3 midCol = vec3(0.9, 0.86, 0.8);
  vec3 hiCol = vec3(1.02, 0.96, 0.88);
  vec3 ramp = mix(shadowCol, midCol, smoothstep(0.0, 0.5, cel));
  ramp = mix(ramp, hiCol, smoothstep(0.65, 1.0, cel));

  vec3 alb = floor(base.rgb * posterize + 0.5) / posterize;

  vec3 V = normalize(vCameraPos - vWorldPos);
  float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), rimPower);
  vec3 rimCol = vec3(0.35, 0.65, 0.95) * rim * rimStrength;

  // Clamp radiance so gallery/key lights can't blow cel paint to white.
  vec3 Lc = min(vLightColor, vec3(1.0));
  float ambient = 0.3;
  vec3 lit = alb * (ambient + ramp * Lc * 0.55) + rimCol * alb * 0.22;
  outColor = vec4(clamp(lit, 0.0, 0.96), base.a);
}
