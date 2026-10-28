#version 450
// Screen-space reflections.
// MainTex = lit scene color. DepthTex = hardware D32 (binding 1, .r = NDC z).
// Reconstructs the world normal from depth and ray-marches the reflected ray
// in screen space; where it hits geometry it returns the reflected scene color
// with A = hit validity (0 = no hit), so a caller can blend it over a fallback
// (e.g. env cubemap).
//
// Push:
//   0..15 invViewProj
//   16..18 cameraPos
//   19 nearZ  20 farZ
//   21 texelW  22 texelH
//   23 maxDist  24 stepLen
//   25 maxSteps  26 thickness
//   27 strength  28 enabled  29 bias
//   30 maxRoughness

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
    u.data[0], u.data[1],  u.data[2],  u.data[3],
    u.data[4], u.data[5],  u.data[6],  u.data[7],
    u.data[8], u.data[9],  u.data[10], u.data[11],
    u.data[12], u.data[13], u.data[14], u.data[15]);
}

vec3 camPos() { return vec3(u.data[16], u.data[17], u.data[18]); }
float texelW() { return u.data[21]; }
float texelH() { return u.data[22]; }

vec2 sampleDepthRange(vec2 uv, int requestedMip) {
  uv = clamp(uv, vec2(0.0), vec2(1.0));
  if (u.data[31] < 0.5) {
    float depth = texture(DepthTex, uv).r;
    return vec2(depth);
  }
  int mip = clamp(requestedMip, 0, max(int(floor(u.data[31])) - 1, 0));
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

float sampleDepth(vec2 uv) {
  return sampleDepthRange(uv, 0).r;
}

vec3 sampleLit(vec2 uv) {
  return texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

vec3 sampleLitRough(vec2 uv, float roughness) {
  vec2 radius = vec2(texelW(), texelH()) * (1.0 + roughness * 6.0);
  return sampleLit(uv) * 0.4 +
         (sampleLit(uv + vec2(radius.x, 0.0)) + sampleLit(uv - vec2(radius.x, 0.0)) +
          sampleLit(uv + vec2(0.0, radius.y)) + sampleLit(uv - vec2(0.0, radius.y))) * 0.15;
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depthZ) {
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 clip = vec4(ndc, clamp(depthZ, 0.0, 1.0), 1.0);
  vec4 w = invVP * clip;
  return w.xyz / max(w.w, 1e-6);
}

vec3 reconstructNormal(mat4 invVP, vec2 uv, float depthZ, vec3 pos) {
  vec3 packedNormal = texture(NormalTex, clamp(uv, vec2(0.0), vec2(1.0))).xyz * 2.0 - 1.0;
  if (dot(packedNormal, packedNormal) > 0.25) {
    vec3 n = normalize(packedNormal);
    if (dot(n, camPos() - pos) < 0.0) n = -n;
    return n;
  }
  float dR = sampleDepth(uv + vec2(texelW(), 0.0));
  float dL = sampleDepth(uv - vec2(texelW(), 0.0));
  float dU = sampleDepth(uv + vec2(0.0, texelH()));
  float dD = sampleDepth(uv - vec2(0.0, texelH()));
  vec2 uvH = abs(dR - depthZ) < abs(dL - depthZ) ? uv + vec2(texelW(), 0.0)
                                                 : uv - vec2(texelW(), 0.0);
  float dH = abs(dR - depthZ) < abs(dL - depthZ) ? dR : dL;
  vec2 uvV = abs(dU - depthZ) < abs(dD - depthZ) ? uv + vec2(0.0, texelH())
                                                 : uv - vec2(0.0, texelH());
  float dV = abs(dU - depthZ) < abs(dD - depthZ) ? dU : dD;
  vec3 n = normalize(cross(reconstructWorld(invVP, uvH, dH) - pos,
                           reconstructWorld(invVP, uvV, dV) - pos));
  if (dot(n, camPos() - pos) < 0.0) n = -n;
  return n;
}

void main() {
  vec2 uv = fragUV;
  float depthZ = sampleDepth(uv);
  // Background / no geometry → nothing to reflect.
  if (depthZ > 0.999 || u.data[28] < 0.5) {
    outColor = vec4(0.0);
    return;
  }
  mat4 invVP = loadInvVP();
  mat4 VP = inverse(invVP);
  vec3 pos = reconstructWorld(invVP, uv, depthZ);
  float packedPbr = texture(NormalTex, clamp(uv, vec2(0.0), vec2(1.0))).a;
  uint pbr = uint(round(packedPbr * 255.0));
  float roughness = float(pbr & 7u) / 7.0;
  float metallic = float((pbr >> 3) & 7u) / 7.0;
  if (roughness > clamp(u.data[30], 0.0, 1.0)) {
    outColor = vec4(0.0);
    return;
  }
  vec3 N = reconstructNormal(invVP, uv, depthZ, pos);
  vec3 V = normalize(camPos() - pos);
  vec3 surfaceColor = sampleLit(uv);
  vec3 R = reflect(-V, N);
  // Reflection pointing back toward the camera has nothing in screen space.
  if (dot(R, V) > 0.1) {
    outColor = vec4(0.0);
    return;
  }

  const float maxDist = u.data[23];
  const float stepLen = u.data[24];
  const int steps = int(u.data[25] + 0.5);
  const float thickness = u.data[26];
  const float strength = u.data[27];
  const float bias = u.data[29];
  vec3 rayOrigin = pos + N * max(thickness * 0.02, 0.002);

  float adaptiveStep = stepLen * mix(0.65, 1.35, roughness);
  float rayLen = adaptiveStep;
  float previousRayLen = 0.0;
  vec3 hitColor = vec3(0.0);
  float hitVal = 0.0;
  for (int i = 0; i < steps; ++i) {
    vec3 rayPos = rayOrigin + R * rayLen;
    vec4 clip = VP * vec4(rayPos, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    vec2 sUV = ndc.xy * 0.5 + 0.5;
    if (sUV.x < 0.0 || sUV.x > 1.0 || sUV.y < 0.0 || sUV.y > 1.0 ||
        ndc.z < 0.0 || ndc.z > 1.0)
      break;
    if (u.data[31] > 0.5) {
      float conePixels = 1.0 + roughness * 2.0 * rayLen / max(adaptiveStep, 1e-4);
      int traceMip = clamp(int(floor(log2(max(conePixels, 1.0)))), 0,
                           max(int(floor(u.data[31])) - 1, 0));
      vec2 coarseRange = sampleDepthRange(sUV, traceMip);
      float rangePadding = max(bias, (coarseRange.y - coarseRange.x) * 0.25);
      if (ndc.z < coarseRange.x - rangePadding) {
        previousRayLen = rayLen;
        rayLen += adaptiveStep * exp2(float(traceMip));
        if (rayLen > maxDist) break;
        continue;
      }
    }
    float depthAt = sampleDepth(sUV);
    vec3 scenePos = reconstructWorld(invVP, sUV, depthAt);
    float worldGap = length(rayPos - scenePos);
    // Hit when the ray reaches/passes the surface at this screen pixel.
    if (ndc.z > depthAt - bias && worldGap <= max(thickness, adaptiveStep * 1.25)) {
      float lo = previousRayLen;
      float hi = rayLen;
      vec2 refinedUV = sUV;
      for (int refine = 0; refine < 5; ++refine) {
        float mid = (lo + hi) * 0.5;
        vec4 refineClip = VP * vec4(rayOrigin + R * mid, 1.0);
        vec3 refineNdc = refineClip.xyz / max(refineClip.w, 1e-6);
        vec2 candidateUV = refineNdc.xy * 0.5 + 0.5;
        float candidateDepth = sampleDepth(candidateUV);
        if (refineNdc.z > candidateDepth - bias) {
          hi = mid;
          refinedUV = candidateUV;
        } else {
          lo = mid;
        }
      }
      rayLen = hi;
      sUV = refinedUV;
      vec3 refinedRayPos = rayOrigin + R * rayLen;
      float refinedDepth = sampleDepth(refinedUV);
      vec3 refinedScenePos = reconstructWorld(invVP, refinedUV, refinedDepth);
      float hitAccuracy = 1.0 - smoothstep(thickness * 0.2,
                                           max(thickness, adaptiveStep * 1.25),
                                           length(refinedRayPos - refinedScenePos));
      vec3 hitNormal = texture(NormalTex, clamp(refinedUV, vec2(0.0), vec2(1.0))).xyz * 2.0 - 1.0;
      float hitFacing = dot(hitNormal, hitNormal) > 0.25
                            ? smoothstep(0.02, 0.25, dot(normalize(hitNormal), -R))
                            : 1.0;
      float centerDepth = sampleDepth(refinedUV);
      float depthEdge = max(max(abs(sampleDepth(refinedUV + vec2(texelW(), 0.0)) - centerDepth),
                               abs(sampleDepth(refinedUV - vec2(texelW(), 0.0)) - centerDepth)),
                            max(abs(sampleDepth(refinedUV + vec2(0.0, texelH())) - centerDepth),
                                abs(sampleDepth(refinedUV - vec2(0.0, texelH())) - centerDepth)));
      float continuity = 1.0 - smoothstep(0.002, 0.025, depthEdge);
      hitColor = sampleLitRough(refinedUV, roughness);
      float edge = smoothstep(0.02, 0.12,
                              min(min(sUV.x, sUV.y), min(1.0 - sUV.x, 1.0 - sUV.y)));
      float sourceEdge = smoothstep(0.01, 0.06,
                                    min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
      float distFade = 1.0 - smoothstep(0.0, maxDist, rayLen);
      float roughnessFade = 1.0 - smoothstep(0.35, 1.0, roughness) * 0.85;
      hitVal = strength * edge * sourceEdge * distFade * roughnessFade * hitFacing *
               continuity * hitAccuracy;
      break;
    }
    previousRayLen = rayLen;
    rayLen += adaptiveStep;
    if (rayLen > maxDist) break;
  }
  // Conservatively project the lit color onto the dielectric diffuse basis.
  // The non-negative residual is the replaceable specular component; this
  // keeps direct/indirect diffuse intact while SSR replaces probe/sky IBL.
  vec3 materialAlbedo = texture(AlbedoTex, uv).rgb;
  vec3 diffuseBasis = materialAlbedo * (1.0 - metallic);
  vec3 safeBasis = max(diffuseBasis, vec3(1e-3));
  float diffuseScale = max(min(surfaceColor.r / safeBasis.r,
                               min(surfaceColor.g / safeBasis.g,
                                   surfaceColor.b / safeBasis.b)), 0.0);
  vec3 diffuseEstimate = min(diffuseBasis * diffuseScale, surfaceColor);
  vec3 F0 = mix(vec3(0.04), materialAlbedo, metallic);
  float NoV = max(dot(N, V), 0.0);
  vec4 brdf = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) +
              vec4(1.0, 0.0425, 1.04, -0.04);
  float a004 = min(brdf.x * brdf.x, exp2(-9.28 * NoV)) * brdf.x + brdf.y;
  vec2 dfg = vec2(-1.04, 1.04) * a004 + brdf.zw;
  vec3 specWeight = max(F0 * dfg.x + dfg.y, vec3(0.0));
  float directionalAlbedo = max(dfg.x + dfg.y, 1e-3);
  vec3 multiScatter = 1.0 + F0 * (min(1.0 / directionalAlbedo, 8.0) - 1.0);
  vec3 reflectedScene = diffuseEstimate + hitColor * specWeight * multiScatter;
  float confidence = clamp(hitVal, 0.0, 1.0);
  outColor = vec4(reflectedScene * confidence, confidence);
}
