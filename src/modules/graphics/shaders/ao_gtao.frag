#version 450
// Ground-Truth Ambient Occlusion (Jimenez / Frostbite inspired, single-frame).
// Cosine-weighted visibility along horizon arcs in screen space.
// MainTex.r = linear depth [0,1]. Output: RGB = AO, A = depth01.
//
// Push:
//  0..15 invViewProj
//  16 nearZ  17 farZ
//  18 radius  19 bias  20 intensity  21 power
//  22 dirCount  23 stepCount  24 texelW  25 texelH
//  26 thickness

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

float gtaoIntegrate(float h1, float h2) {
  // Integral of cos(theta) over [h1, h2] with n=0 (simplified).
  return clamp(0.25 * (-cos(2.0 * h2) + cos(2.0 * h1) + 2.0 * (h2 - h1)), 0.0, 1.0);
}

void main() {
  mat4 invVP = loadInvVP();
  float nearZ = max(u.data[16], 1e-3);
  float farZ = max(u.data[17], nearZ + 1e-3);
  float radius = max(u.data[18], 1e-4);
  float bias = max(u.data[19], 0.0);
  float intensity = max(u.data[20], 0.0);
  float power = max(u.data[21], 0.01);
  int dirs = int(clamp(u.data[22], 2.0, 8.0));
  int steps = int(clamp(u.data[23], 2.0, 8.0));
  float texelW = max(u.data[24], 1e-5);
  float texelH = max(u.data[25], 1e-5);
  float thickness = max(u.data[26], radius * 0.5);

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
  vec3 viewDir = normalize(camPos - pos);
  if (dot(normal, viewDir) < 0.0) normal = -normal;

  float linearZ = mix(nearZ, farZ, depth01);
  float screenRadius = clamp((radius / max(linearZ, 1e-3)) * 0.4, texelW * 4.0, 0.25);
  float rnd = hash12(fragUV * vec2(640.0, 480.0));
  const float PI = 3.14159265;
  const float HALF_PI = 1.5707963;

  float visibility = 0.0;
  for (int d = 0; d < 8; ++d) {
    if (d >= dirs) break;
    float slice = (float(d) + rnd) / float(dirs) * PI;
    vec2 dir = vec2(cos(slice), sin(slice));

    float hPos = -HALF_PI + bias;
    float hNeg = -HALF_PI + bias;

    for (int s = 1; s <= 8; ++s) {
      if (s > steps) break;
      float t = float(s) / float(steps);

      vec2 uvP = fragUV + dir * (screenRadius * t);
      float dP = sampleDepth(uvP);
      vec3 pP = reconstructWorld(invVP, uvP, dP);
      vec3 dltP = pP - pos;
      float distP = length(dltP);
      if (distP > 1e-4 && distP < radius) {
        float h = asin(clamp(dot(normalize(dltP), normal), -1.0, 1.0));
        float att = 1.0 - distP / radius;
        hPos = max(hPos, mix(-HALF_PI, h, att));
      }

      vec2 uvN = fragUV - dir * (screenRadius * t);
      float dN = sampleDepth(uvN);
      vec3 pN = reconstructWorld(invVP, uvN, dN);
      vec3 dltN = pN - pos;
      float distN = length(dltN);
      if (distN > 1e-4 && distN < radius) {
        float h = asin(clamp(dot(normalize(dltN), normal), -1.0, 1.0));
        float att = 1.0 - distN / radius;
        hNeg = max(hNeg, mix(-HALF_PI, h, att));
      }
    }

    hPos = clamp(hPos, -HALF_PI, HALF_PI);
    hNeg = clamp(hNeg, -HALF_PI, HALF_PI);
    // Dual-horizon: integrate both sides of the slice.
    float contrib = 0.5 * (gtaoIntegrate(-HALF_PI, hPos) + gtaoIntegrate(-HALF_PI, hNeg));
    float thin = smoothstep(0.0, 1.0, (hPos + hNeg + PI) / max(thickness / max(radius, 1e-3), 1e-3));
    visibility += mix(contrib, max(contrib, 0.85), 0.2 * (1.0 - thin));
  }

  visibility /= float(max(dirs, 1));
  float ao = clamp(pow(max(visibility, 0.0), power), 0.0, 1.0);
  ao = mix(1.0, ao, clamp(intensity, 0.0, 2.0));
  outColor = vec4(ao, ao, ao, depth01) * fragColor;
}
