#version 450
// Procedural slab volumetric clouds. The depth texture limits cloud compositing
// against scene geometry; cloud density comes from weather-scale 2D coverage,
// height profile and domain-warped 3D fBm.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

mat4 loadInvVP() {
  return mat4(u.data[0], u.data[1], u.data[2], u.data[3],
              u.data[4], u.data[5], u.data[6], u.data[7],
              u.data[8], u.data[9], u.data[10], u.data[11],
              u.data[12], u.data[13], u.data[14], u.data[15]);
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float depth01) {
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec4 w = invVP * vec4(ndc, clamp(depth01, 0.0, 1.0), 1.0);
  return w.xyz / max(abs(w.w), 1e-6);
}

float hash31(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = hash31(i);
  float n100 = hash31(i + vec3(1,0,0));
  float n010 = hash31(i + vec3(0,1,0));
  float n110 = hash31(i + vec3(1,1,0));
  float n001 = hash31(i + vec3(0,0,1));
  float n101 = hash31(i + vec3(1,0,1));
  float n011 = hash31(i + vec3(0,1,1));
  float n111 = hash31(i + vec3(1,1,1));
  return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),
             mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);
}

float fbm(vec3 p) {
  float sum = 0.0, amp = 0.55;
  for (int i = 0; i < 4; ++i) {
    sum += valueNoise(p) * amp;
    p = p * 2.03 + vec3(7.1, 3.7, 5.9);
    amp *= 0.5;
  }
  return sum;
}

float cloudDensity(vec3 worldPos) {
  float bottom = u.data[20], top = max(u.data[21], bottom + 0.01);
  float h = clamp((worldPos.y - bottom) / (top - bottom), 0.0, 1.0);
  float profile = smoothstep(0.0, 0.16, h) * (1.0 - smoothstep(0.68, 1.0, h));
  float scale = max(u.data[24], 0.01);
  vec2 wind = vec2(u.data[25], u.data[26]) * u.data[19];
  vec3 p = vec3((worldPos.xz - wind) / scale, h * 2.7).xzy;
  float weather = fbm(vec3(p.xz * 0.23, 4.2));
  float shape = fbm(p + vec3(weather * 0.7));
  float coverage = clamp(u.data[22], 0.0, 1.0);
  float base = smoothstep(1.02 - coverage, 1.12 - coverage, shape * profile);
  float erosion = valueNoise(p * 5.1 + vec3(11.7, 5.3, 3.1));
  base -= (1.0 - erosion) * 0.22 * (1.0 - base);
  return max(base, 0.0) * max(u.data[23], 0.0);
}

float hg(float mu, float g) {
  float g2 = g * g;
  return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(0.01, 1.0 + g2 - 2.0*g*mu), 1.5));
}

float lightTransmittance(vec3 p, vec3 lightDir, int steps, float stepLen) {
  float opticalDepth = 0.0;
  for (int j = 0; j < 12; ++j) {
    if (j >= steps) break;
    p += lightDir * stepLen;
    opticalDepth += cloudDensity(p) * stepLen;
  }
  return exp(-opticalDepth * 0.85);
}

void main() {
  mat4 invVP = loadInvVP();
  vec3 sunDir = normalize(vec3(u.data[16], u.data[17], u.data[18]));
  vec3 lightColor = max(vec3(u.data[29], u.data[30], u.data[31]), vec3(0.0));
  int samples = int(clamp(u.data[27], 8.0, 96.0));
  int shadowSteps = int(clamp(u.data[28], 1.0, 12.0));
  float depth = texture(MainTex, fragUV).r;
  if (depth < 1e-5) depth = 1.0;
  vec3 cameraPos = reconstructWorld(invVP, vec2(0.5), 0.0);
  vec3 scenePos = reconstructWorld(invVP, fragUV, depth);
  vec3 ray = scenePos - cameraPos;
  float sceneDistance = length(ray);
  vec3 rayDir = ray / max(sceneDistance, 1e-5);

  float bottom = u.data[20], top = max(u.data[21], bottom + 0.01);
  float invY = 1.0 / (abs(rayDir.y) < 1e-5 ? (rayDir.y < 0.0 ? -1e-5 : 1e-5) : rayDir.y);
  float t0 = (bottom - cameraPos.y) * invY;
  float t1 = (top - cameraPos.y) * invY;
  float enterT = max(min(t0, t1), 0.0);
  float exitT = min(max(t0, t1), sceneDistance);
  if (exitT <= enterT) { outColor = vec4(0.0); return; }

  float stepLen = (exitT - enterT) / float(samples);
  float jitter = hash31(vec3(fragUV * vec2(1920.0, 1080.0), u.data[19])) - 0.5;
  vec3 p = cameraPos + rayDir * (enterT + stepLen * (0.5 + jitter * 0.65));
  float transmittance = 1.0;
  vec3 radiance = vec3(0.0);
  float mu = dot(rayDir, sunDir);
  float phase = hg(mu, 0.72) * 0.82 + hg(mu, -0.25) * 0.18;
  float shadowStep = max((top - bottom) / float(shadowSteps), stepLen * 0.75);

  for (int i = 0; i < 96; ++i) {
    if (i >= samples) break;
    float density = cloudDensity(p);
    if (density > 0.002) {
      float lightT = lightTransmittance(p, sunDir, shadowSteps, shadowStep);
      float powder = 1.0 - exp(-density * stepLen * 2.0);
      float heightLight = clamp((p.y - bottom) / max(top - bottom, 0.01), 0.0, 1.0);
      float edgeGlow = exp(-density * stepLen * 1.7) * lightT;
      vec3 ambient = mix(vec3(0.10, 0.14, 0.23), lightColor * 0.32,
                         0.25 + heightLight * 0.55);
      // Artistic multiple-scattering approximation: boosted forward phase,
      // powder brightening and a low-density silver lining.  It preserves the
      // Beer-Lambert silhouette while avoiding charcoal clouds at golden hour.
      vec3 source = lightColor * (phase * lightT * 9.0 + powder * 0.72 + edgeGlow * 0.16)
                    + ambient;
      float extinction = density * stepLen;
      float alpha = 1.0 - exp(-extinction);
      radiance += transmittance * source * alpha;
      transmittance *= 1.0 - alpha;
      if (transmittance < 0.015) break;
    }
    p += rayDir * stepLen;
  }
  outColor = vec4(radiance * fragColor.rgb * 1.65,
                  clamp(1.0 - transmittance, 0.0, 1.0));
}
