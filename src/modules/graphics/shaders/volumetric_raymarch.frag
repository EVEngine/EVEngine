#version 450
// Volumetric ray march through participating media (dust/fog).
// MainTex.r = linear depth in [0,1] (0=near, 1=far/sky). ZO clip compatible.
// Uses invViewProj to rebuild world positions; screen-space marches toward the
// light UV approximate directional CSM occlusion (single sampler constraint).
//
// Push (declare order):
//  0..15 invViewProj (mat4)
//  16..18 lightDir (world, toward surface)
//  19 density
//  20..22 shaft RGB
//  23 intensity
//  24 nearZ  25 farZ
//  26 sampleCount
//  27 dustAmount
//  28 fogAmount
//  29 shadowAnisoPack — floor = shadow steps, fract encodes anisotropy in [-0.99,0.99]
//  30 lightU  31 lightV   — light screen UV for SS shadow

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

mat4 loadInvVP() {
  return mat4(
    u.data[0],  u.data[1],  u.data[2],  u.data[3],
    u.data[4],  u.data[5],  u.data[6],  u.data[7],
    u.data[8],  u.data[9],  u.data[10], u.data[11],
    u.data[12], u.data[13], u.data[14], u.data[15]);
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depth01) {
  // Vulkan RH + ZO: NDC z in [0,1]. UV Y-down matches Vulkan NDC (perspectiveVulkanRH_ZO).
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 clip = vec4(ndc, clamp(depth01, 0.0, 1.0), 1.0);
  vec4 w = invVP * clip;
  return w.xyz / max(w.w, 1e-6);
}

float henyeyGreenstein(float cosTheta, float g) {
  float g2 = g * g;
  float denom = max(1e-4, 1.0 + g2 - 2.0 * g * cosTheta);
  return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

// Dual-lobe HG: strong forward shaft + soft backscatter (HDRP-style).
float dualLobeHG(float cosTheta, float g) {
  float forward = henyeyGreenstein(cosTheta, g);
  float back = henyeyGreenstein(cosTheta, -g * 0.5);
  return mix(back, forward, 0.75);
}

// Soft screen-space shadow: accumulate occlusion toward the light UV.
float screenSpaceShadow(vec2 startUV, float startDepth, vec2 lightUV, int steps) {
  if (steps < 1) return 1.0;
  vec2 delta = (lightUV - startUV) / float(steps);
  vec2 coord = startUV;
  float shadow = 1.0;
  float penumbra = 1.0;
  for (int i = 0; i < 32; ++i) {
    if (i >= steps) break;
    coord += delta;
    if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) break;
    float d = texture(MainTex, coord).r;
    float t = float(i + 1) / float(steps);
    float expected = mix(startDepth, 0.0, t);
    float blocker = expected - d;
    if (blocker > 0.002 && d + 0.003 < startDepth) {
      // Soften with distance from the first blocker (penumbra).
      penumbra = min(penumbra, smoothstep(0.03, 0.0, blocker));
      shadow *= mix(0.12, 0.55, penumbra);
      if (shadow < 0.08) break;
    }
  }
  return clamp(shadow, 0.0, 1.0);
}

void main() {
  mat4 invVP = loadInvVP();
  vec3 lightDir = normalize(vec3(u.data[16], u.data[17], u.data[18]));
  float density = max(u.data[19], 0.0);
  vec3 shaftTint = vec3(u.data[20], u.data[21], u.data[22]);
  float intensity = u.data[23];
  float nearZ = max(u.data[24], 1e-3);
  float farZ = max(u.data[25], nearZ + 1e-3);
  int samples = int(clamp(u.data[26], 4.0, 64.0));
  float dustAmount = u.data[27];
  float fogAmount = u.data[28];
  float shadowAnisoPack = u.data[29];
  int shadowSteps = int(clamp(floor(shadowAnisoPack), 0.0, 32.0));
  float anisoFrac = fract(shadowAnisoPack);
  float g = clamp(anisoFrac * 1.98 - 0.99, -0.99, 0.99);
  // Legacy uploads that only wrote an integer shadow step count.
  if (anisoFrac < 1e-5) g = 0.6;
  vec2 lightUV = vec2(u.data[30], u.data[31]);

  float depth01 = texture(MainTex, fragUV).r;
  if (depth01 < 1e-5) depth01 = 1.0; // empty → march to far

  vec3 camPos = reconstructWorld(invVP, vec2(0.5, 0.5), 0.0);
  {
    vec4 e = invVP * vec4(0.0, 0.0, 0.0, 1.0);
    camPos = e.xyz / max(e.w, 1e-6);
  }
  vec3 endPos = reconstructWorld(invVP, fragUV, depth01);
  vec3 ray = endPos - camPos;
  float maxDist = length(ray);
  vec3 rayDir = ray / max(maxDist, 1e-5);

  // Spatial dither offsets the first step to break banding without extra push slots.
  float jitter = hash12(fragUV * vec2(917.0, 613.0) + vec2(density, fogAmount));
  float stepLen = maxDist / float(samples);
  vec3 pos = camPos + rayDir * stepLen * jitter;
  float transmittance = 1.0;
  vec3 scatter = vec3(0.0);

  mat4 viewProj = inverse(invVP);

  for (int i = 0; i < 64; ++i) {
    if (i >= samples) break;
    pos += rayDir * stepLen;
    float t = (float(i) + jitter) / float(samples);

    vec2 sampleUV = mix(vec2(0.5), fragUV, clamp(t, 0.0, 1.0));
    vec4 clip = viewProj * vec4(pos, 1.0);
    vec2 projUV = clip.xy / max(clip.w, 1e-6);
    projUV = vec2(projUV.x * 0.5 + 0.5, 1.0 - (projUV.y * 0.5 + 0.5));
    if (projUV.x >= 0.0 && projUV.x <= 1.0 && projUV.y >= 0.0 && projUV.y <= 1.0)
      sampleUV = projUV;

    float sampleDepth = texture(MainTex, sampleUV).r;
    float vis = screenSpaceShadow(sampleUV, sampleDepth, lightUV, shadowSteps);

    // Height-modulated media: denser near the ground for god-ray shafts.
    float heightFactor = exp(-0.04 * max(pos.y, 0.0));
    heightFactor = max(heightFactor, 0.35);

    float cosTheta = dot(-rayDir, lightDir);
    float phase = dualLobeHG(cosTheta, g);
    float dens = density * (1.0 + fogAmount * 0.5) * heightFactor;
    // Mild coherent wisps so shafts are not perfectly uniform.
    float wisp = 0.85 + 0.30 * hash13(pos * 0.15);
    dens *= wisp;

    float stepScatter = dens * phase * vis * stepLen;
    scatter += shaftTint * stepScatter * transmittance * intensity;
    transmittance *= exp(-dens * stepLen * 0.85);
    if (transmittance < 0.01) break;
  }

  // Dust scintillation along the ray, stronger on the light axis.
  float axis = exp(-length(fragUV - lightUV) * 3.0);
  float n = hash12(fragUV * vec2(500.0, 280.0) + vec2(dustAmount, fogAmount));
  scatter += shaftTint * n * dustAmount * intensity * (0.10 + 0.20 * axis);

  // Soft additive alpha so overlapping shafts do not hard-clip.
  float a = clamp(1.0 - exp(-dot(scatter, vec3(0.2126, 0.7152, 0.0722)) * 1.15), 0.0, 1.0);
  outColor = vec4(scatter * fragColor.rgb, a);
}
