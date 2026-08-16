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

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
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

float sampleDepth(vec2 uv) {
  return texture(DepthTex, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

vec3 sampleLit(vec2 uv) {
  return texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depthZ) {
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 clip = vec4(ndc, clamp(depthZ, 0.0, 1.0), 1.0);
  vec4 w = invVP * clip;
  return w.xyz / max(w.w, 1e-6);
}

vec3 reconstructNormal(mat4 invVP, vec2 uv, float depthZ, vec3 pos) {
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
  vec3 N = reconstructNormal(invVP, uv, depthZ, pos);
  vec3 V = normalize(camPos() - pos);
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

  float rayLen = stepLen;
  vec3 hitColor = vec3(0.0);
  float hitVal = 0.0;
  for (int i = 0; i < steps; ++i) {
    vec3 rayPos = pos + R * rayLen;
    vec4 clip = VP * vec4(rayPos, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    vec2 sUV = ndc.xy * 0.5 + 0.5;
    if (sUV.x < 0.0 || sUV.x > 1.0 || sUV.y < 0.0 || sUV.y > 1.0 ||
        ndc.z < 0.0 || ndc.z > 1.0)
      break;
    float depthAt = sampleDepth(sUV);
    // Hit when the ray reaches/passes the surface at this screen pixel.
    if (rayLen > thickness && ndc.z > depthAt - bias) {
      hitColor = sampleLit(sUV);
      float edge = 1.0 - smoothstep(0.04, 0.16,
                                    min(min(sUV.x, sUV.y), min(1.0 - sUV.x, 1.0 - sUV.y)));
      float distFade = 1.0 - smoothstep(0.0, maxDist, rayLen);
      hitVal = strength * edge * distFade;
      break;
    }
    rayLen += stepLen;
    if (rayLen > maxDist) break;
  }
  outColor = vec4(hitColor * hitVal, hitVal);
}
