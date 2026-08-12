#version 450
// Volumetric height + distance fog (Wronski/Frostbite-inspired, single-sampler).
// MainTex.r = linear depth [0,1]. Reconstructs world positions via invViewProj,
// integrates Beer-Lambert with exponential height falloff and distance ramp.
// Output is non-premultiplied RGB + opacity for the engine's SrcAlpha blend.
//
// Push:
//  0..15 invViewProj
//  16..18 lightDir (world, toward surface) — soft sun tint
//  19 density
//  20..22 fog RGB
//  23 intensity
//  24 nearZ  25 farZ
//  26 sampleCount
//  27 fogHeight (world Y base)
//  28 heightFalloff
//  29 fogStart  30 fogEnd  (view-space distance ramp)
//  31 noiseAmount

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

mat4 loadInvVP() {
  return mat4(
    u.data[0],  u.data[1],  u.data[2],  u.data[3],
    u.data[4],  u.data[5],  u.data[6],  u.data[7],
    u.data[8],  u.data[9],  u.data[10], u.data[11],
    u.data[12], u.data[13], u.data[14], u.data[15]);
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depth01) {
  // Vulkan NDC Y-down (matches perspectiveVulkanRH_ZO).
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

void main() {
  mat4 invVP = loadInvVP();
  vec3 lightDir = normalize(vec3(u.data[16], u.data[17], u.data[18]) + vec3(1e-5));
  float density = max(u.data[19], 0.0);
  vec3 fogColor = vec3(u.data[20], u.data[21], u.data[22]);
  float intensity = max(u.data[23], 0.0);
  float nearZ = max(u.data[24], 1e-3);
  float farZ = max(u.data[25], nearZ + 1e-3);
  int samples = int(clamp(u.data[26], 4.0, 64.0));
  float fogHeight = u.data[27];
  float heightFalloff = max(u.data[28], 0.0);
  float fogStart = max(u.data[29], 0.0);
  float fogEnd = max(u.data[30], fogStart + 1e-3);
  float noiseAmount = clamp(u.data[31], 0.0, 2.0);

  float depth01 = texture(MainTex, fragUV).r;
  if (depth01 < 1e-5) depth01 = 1.0;

  // Linear depth encoding used by Volumetric::newLinearDepthTexture.
  float linearZ = mix(nearZ, farZ, clamp(depth01, 0.0, 1.0));

  vec4 eyeH = invVP * vec4(0.0, 0.0, 0.0, 1.0);
  vec3 camPos = eyeH.xyz / max(eyeH.w, 1e-6);
  vec3 endPos = reconstructWorld(invVP, fragUV, depth01);
  vec3 ray = endPos - camPos;
  float reconDist = length(ray);
  // Prefer the larger of reconstructed / linear depth so short NDC reconstructions
  // still accumulate fog when the depth buffer says the hit is far.
  float maxDist = max(reconDist, linearZ);
  vec3 rayDir = (reconDist > 1e-4) ? (ray / reconDist)
                                   : normalize(reconstructWorld(invVP, fragUV, 1.0) - camPos);

  float stepLen = maxDist / float(samples);
  vec3 pos = camPos;
  float transmittance = 1.0;
  vec3 inScatter = vec3(0.0);
  float g = 0.35;

  for (int i = 0; i < 64; ++i) {
    if (i >= samples) break;
    float t = (float(i) + 0.5) / float(samples);
    pos = camPos + rayDir * (t * maxDist);
    float dist = t * maxDist;

    // Soft distance ramp (0 before fogStart → 1 at fogEnd).
    float distFactor = smoothstep(fogStart, fogEnd, dist);
    // Keep a little participating media even before fogStart so near hits still fog.
    distFactor = max(distFactor, 0.15 * clamp(dist / max(fogEnd, 1e-3), 0.0, 1.0));

    // Exponential height fog: denser near / below fogHeight.
    float h = pos.y - fogHeight;
    float heightFactor = exp(-heightFalloff * max(h, 0.0));
    // Soft floor so elevated cameras still see distance fog.
    heightFactor = max(heightFactor, 0.2);

    float n = 1.0 + (hash12(pos.xz * 0.15 + fragUV * 3.0) - 0.5) * noiseAmount;
    float dens = density * heightFactor * distFactor * max(n, 0.05);

    float cosTheta = dot(-rayDir, lightDir);
    float phase = mix(1.0, henyeyGreenstein(cosTheta, g) * 4.0, 0.35);
    float stepOptical = dens * stepLen;
    inScatter += fogColor * phase * stepOptical * transmittance * intensity;
    transmittance *= exp(-stepOptical);
    if (transmittance < 0.01) break;
  }

  float a = clamp(1.0 - transmittance, 0.0, 1.0);
  // Engine textured pipelines blend with SrcAlpha / OneMinusSrcAlpha, so RGB must be
  // non-premultiplied (opacity carried only in alpha).
  vec3 color = (a > 1e-4) ? clamp(inScatter / a, 0.0, 4.0) : fogColor * intensity;
  outColor = vec4(color * fragColor.rgb, a);
}
