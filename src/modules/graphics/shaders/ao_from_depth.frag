#version 450
// Combined SSAO + overlay for the 3D swapchain pass.
// Samples hardware D32 (MainTex.r = Vulkan NDC z). Optional NormalTex
// (binding 1) is GBuffer world normal * 0.5 + 0.5. World-space hemisphere
// (not a screen disc) so nearby props do not stamp swimming silhouettes
// onto the floor.
//
// Push (declareMatrix + declareFloat order):
//  0..15 invViewProj
//  16 nearZ  17 farZ
//  18 radius  19 bias  20 intensity  21 power
//  22 sampleCount  23 texelW  24 texelH  25 hasNormal

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D NormalTex;
layout(push_constant) uniform Externals { float data[32]; } u;

mat4 loadInvVP() {
  return mat4(
    u.data[0],  u.data[1],  u.data[2],  u.data[3],
    u.data[4],  u.data[5],  u.data[6],  u.data[7],
    u.data[8],  u.data[9],  u.data[10], u.data[11],
    u.data[12], u.data[13], u.data[14], u.data[15]);
}

float ndcToViewZ(float ndcZ, float nearZ, float farZ) {
  return (nearZ * farZ) / max(farZ - ndcZ * (farZ - nearZ), 1e-6);
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float ndcZ) {
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 clip = vec4(ndc, clamp(ndcZ, 0.0, 1.0), 1.0);
  vec4 w = invVP * clip;
  return w.xyz / max(w.w, 1e-6);
}

float sampleNdcZ(vec2 uv) {
  return texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

vec3 kernelSample(int i) {
  float fi = float(i);
  float z = fract(fi * 0.6180339887);
  float a = fi * 2.3999632297;
  float r = sqrt(max(1.0 - z * z, 0.0)) * mix(0.2, 1.0, (fi + 1.0) / 24.0);
  return vec3(cos(a) * r, sin(a) * r, z);
}

void main() {
  mat4 invVP = loadInvVP();
  float nearZ = max(u.data[16], 1e-3);
  float farZ = max(u.data[17], nearZ + 1e-3);
  float radius = max(u.data[18], 1e-4);
  float bias = max(u.data[19], 0.0);
  float intensity = max(u.data[20], 0.0);
  float power = max(u.data[21], 0.01);
  int samples = int(clamp(u.data[22], 4.0, 24.0));
  float texelW = max(u.data[23], 1e-5);
  float texelH = max(u.data[24], 1e-5);
  float hasNormal = u.data[25];

  float ndcZ = sampleNdcZ(fragUV);
  if (ndcZ >= 0.9999) {
    outColor = vec4(0.0);
    return;
  }

  vec3 pos = reconstructWorld(invVP, fragUV, ndcZ);
  vec4 eyeH = invVP * vec4(0.0, 0.0, 0.0, 1.0);
  vec3 camPos = eyeH.xyz / max(eyeH.w, 1e-6);
  vec3 normal;
  if (hasNormal > 0.5) {
    vec3 n = texture(NormalTex, clamp(fragUV, vec2(0.0), vec2(1.0))).xyz * 2.0 - 1.0;
    float len = length(n);
    normal = len > 1e-3 ? n / len : vec3(0.0, 1.0, 0.0);
  } else {
    vec3 posR = reconstructWorld(invVP, fragUV + vec2(texelW, 0.0),
                                 sampleNdcZ(fragUV + vec2(texelW, 0.0)));
    vec3 posL = reconstructWorld(invVP, fragUV - vec2(texelW, 0.0),
                                 sampleNdcZ(fragUV - vec2(texelW, 0.0)));
    vec3 posU = reconstructWorld(invVP, fragUV + vec2(0.0, texelH),
                                 sampleNdcZ(fragUV + vec2(0.0, texelH)));
    vec3 posD = reconstructWorld(invVP, fragUV - vec2(0.0, texelH),
                                 sampleNdcZ(fragUV - vec2(0.0, texelH)));
    vec3 h = abs(sampleNdcZ(fragUV + vec2(texelW, 0.0)) - ndcZ) <
                     abs(sampleNdcZ(fragUV - vec2(texelW, 0.0)) - ndcZ)
                 ? (posR - pos)
                 : (pos - posL);
    vec3 v = abs(sampleNdcZ(fragUV + vec2(0.0, texelH)) - ndcZ) <
                     abs(sampleNdcZ(fragUV - vec2(0.0, texelH)) - ndcZ)
                 ? (posU - pos)
                 : (pos - posD);
    normal = normalize(cross(h, v));
  }
  if (dot(normal, camPos - pos) < 0.0) normal = -normal;

  vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(up - normal * dot(up, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 tbn = mat3(tangent, bitangent, normal);
  mat4 vp = inverse(invVP);

  float occlusion = 0.0;
  for (int i = 0; i < 24; ++i) {
    if (i >= samples) break;
    vec3 sampleWorld = pos + tbn * (kernelSample(i) * radius);
    vec4 clip = vp * vec4(sampleWorld, 1.0);
    if (clip.w < 1e-5) continue;
    vec3 ndc = clip.xyz / clip.w;
    vec2 sampleUV = ndc.xy * 0.5 + 0.5;
    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
      continue;
    float sampleNdc = sampleNdcZ(sampleUV);
    if (sampleNdc >= 0.9999) continue;

    vec3 hitPos = reconstructWorld(invVP, sampleUV, sampleNdc);
    float dist = length(hitPos - pos);
    if (dist < 1e-5 || dist > radius) continue;
    float ndl = max(dot(normal, (hitPos - pos) / dist), 0.0);

    float expectedV = ndcToViewZ(clamp(ndc.z, 0.0, 1.0), nearZ, farZ);
    float actualV = ndcToViewZ(sampleNdc, nearZ, farZ);
    float delta = expectedV - actualV;
    float depthOcc = smoothstep(bias, bias + radius * 0.08, delta);
    float thick = 1.0 - smoothstep(radius * 0.2, radius * 0.55, max(delta, 0.0));
    float mismatch = length(hitPos - sampleWorld);
    float local = 1.0 - smoothstep(radius * 0.25, radius * 0.7, mismatch);
    float rangeCheck = 1.0 - smoothstep(radius * 0.4, radius, dist);
    occlusion += ndl * depthOcc * thick * local * rangeCheck;
  }

  float ao = 1.0 - (occlusion / float(max(samples, 1)));
  ao = clamp(pow(max(ao, 0.0), power), 0.0, 1.0);
  float dark = clamp((1.0 - ao) * intensity, 0.0, 1.0);
  outColor = vec4(0.0, 0.0, 0.0, dark) * fragColor;
}
