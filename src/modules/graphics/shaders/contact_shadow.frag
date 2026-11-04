#version 450
// Screen-space contact shadows: short ray march along the primary light
 // direction against GBuffer hardware depth (MainTex.r = Vulkan NDC z).
// Optional NormalTex (binding 1) is world normal * 0.5 + 0.5.
//
// Push (declareMatrix + declareFloat order):
//  0..15 invViewProj
//  16 nearZ  17 farZ
//  18 length  19 thickness  20 intensity  21 stepCount
 //  22 lightDirX  23 lightDirY  24 lightDirZ
//  25 texelW  26 texelH  27 hasNormal  28 startBias

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

void main() {
  mat4 invVP = loadInvVP();
  float nearZ = max(u.data[16], 1e-3);
  float farZ = max(u.data[17], nearZ + 1e-3);
  float rayLen = max(u.data[18], 1e-4);
  float thickness = max(u.data[19], 1e-4);
  float intensity = max(u.data[20], 0.0);
  int steps = int(clamp(u.data[21], 4.0, 32.0));
  vec3 lightDir = normalize(vec3(u.data[22], u.data[23], u.data[24]));
  float texelW = max(u.data[25], 1e-5);
  float texelH = max(u.data[26], 1e-5);
  float hasNormal = u.data[27];
  float startBias = max(u.data[28], 0.0);

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
    vec3 posU = reconstructWorld(invVP, fragUV + vec2(0.0, texelH),
                                 sampleNdcZ(fragUV + vec2(0.0, texelH)));
    normal = normalize(cross(posR - pos, posU - pos));
  }
  if (dot(normal, camPos - pos) < 0.0) normal = -normal;

  // Only surfaces facing the light can cast contact shadow onto themselves.
  float nDotL = dot(normal, lightDir);
  if (nDotL <= 0.02) {
    outColor = vec4(0.0);
    return;
  }

  mat4 viewProj = inverse(invVP);
  vec3 origin = pos + normal * startBias + lightDir * startBias;
  float occlusion = 0.0;
  float stepSize = rayLen / float(steps);
  for (int i = 1; i <= steps; ++i) {
    vec3 samplePos = origin + lightDir * (stepSize * float(i));
    vec4 clip = viewProj * vec4(samplePos, 1.0);
    if (abs(clip.w) < 1e-6) continue;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

    float sceneNdcZ = sampleNdcZ(uv);
    if (sceneNdcZ >= 0.9999) continue;

    float sceneViewZ = ndcToViewZ(sceneNdcZ, nearZ, farZ);
    float rayViewZ = ndcToViewZ(clamp(ndc.z, 0.0, 1.0), nearZ, farZ);
    float delta = rayViewZ - sceneViewZ;
    // Hit when the ray is behind scene geometry within a contact thickness.
    if (delta > 0.0 && delta < thickness) {
      float fade = 1.0 - float(i) / float(steps);
      float soft = 1.0 - clamp(delta / thickness, 0.0, 1.0);
      occlusion = max(occlusion, fade * soft);
      break;
    }
  }

  float darken = clamp(occlusion * intensity, 0.0, 1.0);
  outColor = vec4(0.0, 0.0, 0.0, darken);
}
