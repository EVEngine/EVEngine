#version 450
// Stylized cel / toon fragment for Mesh3D (Rock demo).
// Uses same Frame UBO + albedo sampler as mesh3d.frag.
// Push constants (optional): data[0]=bands, data[1]=rimPower,
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

  // Soft cel bands
  float cel = floor(ndotl * bands) / max(bands - 1.0, 1.0);
  // Warm shadow / cool mid / warm highlight ramp
  vec3 shadowCol = vec3(0.18, 0.14, 0.28);
  vec3 midCol = vec3(0.85, 0.72, 0.55);
  vec3 hiCol = vec3(1.15, 1.05, 0.90);
  vec3 ramp = mix(shadowCol, midCol, clamp(cel * 1.4, 0.0, 1.0));
  ramp = mix(ramp, hiCol, smoothstep(0.65, 1.0, cel));

  // Posterize albedo toward painted look
  vec3 alb = base.rgb;
  alb = floor(alb * posterize + 0.5) / posterize;

  // Rim light (view-facing edges)
  vec3 V = normalize(vCameraPos - vWorldPos);
  float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), rimPower);
  vec3 rimCol = vec3(0.55, 0.85, 1.15) * rim * rimStrength;

  float ambient = 0.18;
  vec3 lit = alb * (ambient + ramp * vLightColor) + rimCol * alb;
  outColor = vec4(lit, base.a);
}
