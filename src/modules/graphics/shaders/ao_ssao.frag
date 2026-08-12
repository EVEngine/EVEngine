#version 450
// Screen-space Ambient Occlusion (Crytek / Mittring style).
// MainTex.r = linear depth [0,1]. Normals reconstructed from depth derivatives.
// Samples a hemisphere in world space, reprojects via invViewProj trick:
//   clip = inv(invViewProj) * worldHomogeneous is expensive, so we instead
//   march in screen space along projected tangent offsets and compare depths.
// Output: RGB = AO (1=open), A = depth01 (for bilateral blur).
//
// Push (declareMatrix + declareFloat order):
//  0..15 invViewProj
//  16 nearZ  17 farZ
//  18 radius  19 bias  20 intensity  21 power
//  22 sampleCount  23 texelW  24 texelH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

mat4 loadInvVP() {
  return mat4(
    u.data[0],  u.data[1],  u.data[2],  u.data[3],
    u.data[4],  u.data[5],  u.data[6],  u.data[7],
    u.data[8],  u.data[9],  u.data[10], u.data[11],
    u.data[12], u.data[13], u.data[14], u.data[15]);
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depth01) {
  // Vulkan NDC Y-down (matches perspectiveVulkanRH_ZO): UV (0,0)=top-left → NDC (-1,-1).
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 clip = vec4(ndc, clamp(depth01, 0.0, 1.0), 1.0);
  vec4 w = invVP * clip;
  return w.xyz / max(w.w, 1e-6);
}

float sampleDepth(vec2 uv) {
  return texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec3 kernelSample(int i) {
  float fi = float(i);
  float z = fract(fi * 0.6180339887);
  float a = fi * 2.3999632297;
  float r = sqrt(max(1.0 - z * z, 0.0)) * mix(0.15, 1.0, (fi + 1.0) / 24.0);
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

  float depth01 = sampleDepth(fragUV);
  if (depth01 >= 0.999 || depth01 <= 1e-5) {
    outColor = vec4(1.0, 1.0, 1.0, depth01);
    return;
  }

  vec3 pos = reconstructWorld(invVP, fragUV, depth01);
  vec3 posR = reconstructWorld(invVP, fragUV + vec2(texelW, 0.0),
                               sampleDepth(fragUV + vec2(texelW, 0.0)));
  vec3 posU = reconstructWorld(invVP, fragUV + vec2(0.0, texelH),
                               sampleDepth(fragUV + vec2(0.0, texelH)));
  vec3 normal = normalize(cross(posR - pos, posU - pos));
  vec4 eyeH = invVP * vec4(0.0, 0.0, 0.0, 1.0);
  vec3 camPos = eyeH.xyz / max(eyeH.w, 1e-6);
  if (dot(normal, camPos - pos) < 0.0) normal = -normal;

  float rnd = hash12(fragUV * vec2(1024.0, 768.0));
  float ca = cos(rnd * 6.2831853);
  float sa = sin(rnd * 6.2831853);
  vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(up - normal * dot(up, normal));
  vec3 bitangent = cross(normal, tangent);
  tangent = tangent * ca + bitangent * sa;
  bitangent = cross(normal, tangent);
  mat3 tbn = mat3(tangent, bitangent, normal);

  float linearZ = mix(nearZ, farZ, depth01);
  // Screen-space radius in UV units (perspective foreshortening).
  float uvRadius = clamp((radius / max(linearZ, 1e-3)) * 0.25, texelW * 2.0, 0.2);

  float occlusion = 0.0;
  for (int i = 0; i < 24; ++i) {
    if (i >= samples) break;
    vec3 k = kernelSample(i);
    // Screen-space offset along tangent frame (xy) with z used as depth bias scale.
    vec2 offset = (tbn * k).xy;
    // Map world-ish offset to UV using the local tangent projection scale.
    vec2 sampleUV = fragUV + normalize(offset + vec2(1e-5)) * (uvRadius * length(k.xy));
    sampleUV = clamp(sampleUV, vec2(0.0), vec2(1.0));

    float sampleDepth01 = sampleDepth(sampleUV);
    float sampleZ = mix(nearZ, farZ, sampleDepth01);

    // Reconstruct sample world position to get a proper occluder test.
    vec3 samplePos = reconstructWorld(invVP, sampleUV, sampleDepth01);
    vec3 sampleDir = samplePos - pos;
    float dist = length(sampleDir);
    if (dist < 1e-5) continue;

    float ndl = dot(normal, sampleDir / dist);
    // Occluder in front of the hemisphere and within radius.
    float rangeCheck = 1.0 - smoothstep(radius * 0.5, radius, dist);
    float occ = step(bias, ndl) * step(sampleZ + bias * radius, linearZ);
    // Prefer geometry test: sample is closer to camera AND in the hemisphere.
    occ = (ndl > bias && sampleZ < linearZ - bias) ? 1.0 : 0.0;
    occlusion += occ * rangeCheck;
  }

  float ao = 1.0 - (occlusion / float(max(samples, 1))) * clamp(intensity, 0.0, 2.0);
  ao = clamp(pow(max(ao, 0.0), power), 0.0, 1.0);
  outColor = vec4(ao, ao, ao, depth01) * fragColor;
}
