#version 450
// Single-bounce screen-space GI.
// MainTex = scene color (RGB = lit radiance). DepthTex = hardware D32
// (binding 1, .r = Vulkan NDC z) when useNdcDepth != 0; otherwise depth is
// packed in MainTex.a as linear 0..1 (Canvas / unit tests).
//
// Samples a world-space hemisphere (not a screen disc) so nearby props are
// not copied onto the floor as multiple swimming ghosts.
//
// Push:
//  0..15 invViewProj
//  16 nearZ  17 farZ
//  18 radius  19 intensity  20 sampleCount
//  21 lightDirX  22 lightDirY  23 lightDirZ
//  24 lightR  25 lightG  26 lightB
//  27 texelW  28 texelH
//  29 useNdcDepth
//  30 normalValid  31 packed: integer=sampling phase, fractional=world-space thickness

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
layout(binding = 2) uniform sampler2D NormalTex;
layout(binding = 3) uniform sampler2D AlbedoTex;
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

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depthZ) {
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 clip = vec4(ndc, clamp(depthZ, 0.0, 1.0), 1.0);
  vec4 w = invVP * clip;
  return w.xyz / max(w.w, 1e-6);
}

vec2 clampUV(vec2 uv) {
  return clamp(uv, vec2(0.0), vec2(1.0));
}

vec3 sampleLit(vec2 uv) {
  return texture(MainTex, clampUV(uv)).rgb;
}

float sampleDepth(vec2 uv, float useNdc) {
  vec2 c = clampUV(uv);
  float hierarchyLevels = floor(floor(u.data[31]) / 8.0);
  if (useNdc > 0.5) {
    if (hierarchyLevels > 0.5) c.x *= 0.5;
    return texture(DepthTex, c).r;
  }
  return texture(MainTex, c).a;
}

vec2 sampleDepthRange(vec2 uv, int requestedMip) {
  uv = clampUV(uv);
  int levels = int(floor(floor(u.data[31]) / 8.0));
  if (levels <= 0) {
    float depth = texture(DepthTex, uv).r;
    return vec2(depth);
  }
  int mip = clamp(requestedMip, 0, levels - 1);
  ivec2 atlasSize = textureSize(DepthTex, 0);
  int levelWidth = max(atlasSize.x / 2, 1);
  int levelHeight = max(atlasSize.y, 1);
  int offsetX = 0;
  for (int level = 0; level < mip; ++level) {
    offsetX += levelWidth;
    levelWidth = max(levelWidth / 2, 1);
    levelHeight = max(levelHeight / 2, 1);
  }
  ivec2 pixel = ivec2(clamp(floor(uv * vec2(levelWidth, levelHeight)), vec2(0.0),
                             vec2(levelWidth - 1, levelHeight - 1)));
  return texelFetch(DepthTex, ivec2(offsetX + pixel.x, pixel.y), 0).rg;
}

vec3 reconstructNormal(mat4 invVP, vec2 uv, float depthZ, vec3 pos,
                       float texelW, float texelH, float useNdc, vec3 camPos) {
  if (u.data[30] > 0.5) {
    vec3 packed = texture(NormalTex, clampUV(uv)).xyz * 2.0 - 1.0;
    if (dot(packed, packed) > 0.25) {
      vec3 n = normalize(packed);
      if (dot(n, camPos - pos) < 0.0) n = -n;
      return n;
    }
  }
  float dR = sampleDepth(uv + vec2(texelW, 0.0), useNdc);
  float dL = sampleDepth(uv - vec2(texelW, 0.0), useNdc);
  float dU = sampleDepth(uv + vec2(0.0, texelH), useNdc);
  float dD = sampleDepth(uv - vec2(0.0, texelH), useNdc);
  vec2 uvH = abs(dR - depthZ) < abs(dL - depthZ) ? uv + vec2(texelW, 0.0)
                                                 : uv - vec2(texelW, 0.0);
  float dH = abs(dR - depthZ) < abs(dL - depthZ) ? dR : dL;
  vec2 uvV = abs(dU - depthZ) < abs(dD - depthZ) ? uv + vec2(0.0, texelH)
                                                 : uv - vec2(0.0, texelH);
  float dV = abs(dU - depthZ) < abs(dD - depthZ) ? dU : dD;
  vec3 n = normalize(cross(reconstructWorld(invVP, uvH, dH) - pos,
                           reconstructWorld(invVP, uvV, dV) - pos));
  if (dot(n, camPos - pos) < 0.0) n = -n;
  return n;
}

vec3 kernelSample(int i, float phase) {
  float fi = float(i);
  float z = fract((fi + phase * 0.754877666) * 0.6180339887);
  float a = (fi + phase * 0.618033989) * 2.3999632297;
  float r = sqrt(max(1.0 - z * z, 0.0)) * mix(0.25, 1.0, (fi + 1.0) / 32.0);
  return vec3(cos(a) * r, sin(a) * r, z);
}

void main() {
  mat4 invVP = loadInvVP();
  float nearZ = max(u.data[16], 1e-3);
  float farZ = max(u.data[17], nearZ + 1e-3);
  float radius = max(u.data[18], 1e-4);
  float intensity = max(u.data[19], 0.0);
  int samples = int(clamp(u.data[20], 4.0, 32.0));
  float texelW = max(u.data[27], 1e-5);
  float texelH = max(u.data[28], 1e-5);
  float useNdc = u.data[29];
  float phase = mod(floor(u.data[31]), 8.0);
  float thickness = max(fract(u.data[31]), 0.01);
  vec3 lightDir = normalize(vec3(u.data[21], u.data[22], u.data[23]) + vec3(1e-6));
  vec3 lightColor = max(vec3(u.data[24], u.data[25], u.data[26]), vec3(0.0));

  float depthZ = sampleDepth(fragUV, useNdc);
  float skyCut = useNdc > 0.5 ? 0.9999 : 0.999;
  if (depthZ >= skyCut || (useNdc < 0.5 && depthZ <= 1e-5) || intensity < 1e-4) {
    outColor = vec4(0.0);
    return;
  }

  vec3 lit = sampleLit(fragUV);
  vec3 pos = reconstructWorld(invVP, fragUV, depthZ);
  vec4 nearH = invVP * vec4(0.0, 0.0, 0.0, 1.0);
  vec4 farH = invVP * vec4(0.0, 0.0, 1.0, 1.0);
  vec3 nearCenter = nearH.xyz / max(nearH.w, 1e-6);
  vec3 farCenter = farH.xyz / max(farH.w, 1e-6);
  vec3 camPos = nearCenter - normalize(farCenter - nearCenter) * nearZ;
  vec3 normal = reconstructNormal(invVP, fragUV, depthZ, pos, texelW, texelH,
                                  useNdc, camPos);
  float receiverMetallic = 0.0;
  if (u.data[30] > 0.5) {
    uint packedPbr = uint(round(texture(NormalTex, fragUV).a * 255.0));
    receiverMetallic = float((packedPbr >> 3) & 7u) / 7.0;
  }

  vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(up - normal * dot(up, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 tbn = mat3(tangent, bitangent, normal);
  mat4 vp = inverse(invVP);

  vec3 bounce = vec3(0.0);
  float weight = 0.0;
  float hitCount = 0.0;
  for (int i = 0; i < 32; ++i) {
    if (i >= samples) break;
    vec3 sampleWorld = pos + tbn * (kernelSample(i, phase) * radius);
    vec4 clip = vp * vec4(sampleWorld, 1.0);
    if (clip.w < 1e-5) continue;
    vec3 ndc = clip.xyz / clip.w;
    vec2 sampleUV = ndc.xy * 0.5 + 0.5;
    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
      continue;
    int hierarchyLevels = int(floor(floor(u.data[31]) / 8.0));
    if (hierarchyLevels > 0 && useNdc > 0.5) {
      vec2 footprint = abs(sampleUV - fragUV) / vec2(texelW, texelH);
      int mip = clamp(int(floor(log2(max(max(footprint.x, footprint.y) * 0.125, 1.0)))),
                      0, hierarchyLevels - 1);
      vec2 depthRange = sampleDepthRange(sampleUV, mip);
      float depthPadding = max(1e-4, thickness / max(farZ, 1e-3));
      if (ndc.z < depthRange.x - depthPadding || ndc.z > depthRange.y + depthPadding)
        continue;
    }
    float sDepth = sampleDepth(sampleUV, useNdc);
    if (sDepth >= skyCut || (useNdc < 0.5 && sDepth <= 1e-5)) continue;

    vec3 hitPos = reconstructWorld(invVP, sampleUV, sDepth);
    // Reject hits that are not near the hemisphere sample — that is the
    // screen-space copy of a nearby curtain/column onto the floor.
    float mismatch = length(hitPos - sampleWorld);
    if (mismatch > max(thickness, radius * 0.08)) continue;

    vec3 toHit = hitPos - pos;
    float dist = length(toHit);
    if (dist < 1e-4 || dist > radius) continue;
    vec3 Lhit = toHit / dist;
    float ndlRecv = max(dot(normal, Lhit), 0.0);
    if (ndlRecv < 0.04) continue;
    vec3 sN = reconstructNormal(invVP, sampleUV, sDepth, hitPos, texelW, texelH,
                                useNdc, camPos);
    float ndlSend = max(dot(sN, -Lhit), 0.0);
    if (ndlSend < 0.04) continue;
    float directionalEnergy = mix(0.35, 1.0, max(dot(sN, lightDir), 0.0));

    float rangeCheck = 1.0 - smoothstep(radius * 0.4, radius, dist);
    float close = 1.0 - smoothstep(thickness * 0.25, max(thickness, radius * 0.08), mismatch);
    float w = rangeCheck * ndlRecv * ndlSend * close * directionalEnergy;
    bounce += sampleLit(sampleUV) * lightColor * w;
    weight += w;
    hitCount += 1.0;
  }

  if (weight > 1e-4) bounce /= weight;
  float hitCoverage = hitCount / max(float(samples), 1.0);
  bounce *= smoothstep(0.08, 0.35, hitCoverage);

  vec3 receiverAlbedo = useNdc > 0.5 && u.data[30] > 0.5
                            ? texture(AlbedoTex, fragUV).rgb
                            : texture(MainTex, fragUV).rgb;
  // The cosine-weighted sample average estimates irradiance / PI, so the
  // Lambert receiver response is simply albedo times that average.
  bounce *= intensity * receiverAlbedo * (1.0 - receiverMetallic);

  // Store normalized radiance + energy. Additive composition reconstructs
  // bounce exactly as RGB * A without attenuating the lit scene behind it.
  float e = clamp(max(bounce.r, max(bounce.g, bounce.b)), 0.0, 1.0);
  if (e < 1e-4) {
    outColor = vec4(0.0);
    return;
  }
  outColor = vec4(bounce / max(e, 1e-4), e) * fragColor;
}
