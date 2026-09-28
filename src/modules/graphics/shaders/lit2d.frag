#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec2 fragNdc;

layout(set = 0, binding = 0) uniform sampler2D albedoSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;

struct Light2D {
  vec4 posRadius; // xy = point/spot pos OR direction; w = radius (0 => directional)
  vec4 color;     // rgb * intensity
  vec4 spot;      // xy = beam dir; z/w = cos(outer/inner); z <= -1.5 => no cone
};

layout(set = 0, binding = 2) uniform Lighting2D {
  vec4 ambient;   // rgb
  vec4 meta;      // x = lightCount, y = viewW, z = viewH
  Light2D lights[8];
} lighting;

layout(location = 0) out vec4 outColor;

// Rebuild tangent frame from screen/UV derivatives so rotated (and flipped)
// sprites keep the normal map aligned with the albedo. Flat N is +Z (out of
// screen); for axis-aligned UVs this matches the previous (nx, ny, nz) path.
vec3 applyNormalMap2D(vec3 mapSample, vec2 logical, vec2 uv) {
  vec3 mapN = mapSample * 2.0 - 1.0;
  mapN.z = max(mapN.z, 0.05);
  vec3 N = vec3(0.0, 0.0, 1.0);
  vec3 dp1 = vec3(dFdx(logical), 0.0);
  vec3 dp2 = vec3(dFdy(logical), 0.0);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  float det = duv1.x * duv2.y - duv2.x * duv1.y;
  if (abs(det) < 1e-6)
    return normalize(vec3(mapN.xy, mapN.z));
  float invDet = 1.0 / det;
  vec3 T = (dp1 * duv2.y - dp2 * duv1.y) * invDet;
  vec3 B = (dp2 * duv1.x - dp1 * duv2.x) * invDet;
  T = T - N * dot(N, T);
  float tLen = length(T);
  float bLen = length(B);
  if (tLen < 1e-4 || bLen < 1e-4)
    return normalize(vec3(mapN.xy, mapN.z));
  T /= tLen;
  B = normalize(B - N * dot(N, B) - T * dot(T, B));
  if (abs(dot(T, B)) > 0.35)
    return normalize(vec3(mapN.xy, mapN.z));
  return normalize(mat3(T, B, N) * mapN);
}

void main() {
  vec4 base = texture(albedoSampler, fragUV) * fragColor;
  vec2 logical = (fragNdc * 0.5 + 0.5) * lighting.meta.yz;
  vec3 nSample = texture(normalSampler, fragUV).xyz;
  vec3 N = applyNormalMap2D(nSample, logical, fragUV);

  vec3 lit = lighting.ambient.rgb;
  int count = int(lighting.meta.x + 0.5);
  for (int i = 0; i < 8; ++i) {
    if (i >= count) break;
    Light2D L = lighting.lights[i];
    vec3 lightCol = L.color.rgb;
    float contrib = 0.0;
    if (L.posRadius.w <= 0.0) {
      // Directional: xy is light direction toward the surface (or from light).
      vec3 Ld = normalize(vec3(L.posRadius.xy, 0.35));
      contrib = max(dot(N, Ld), 0.0);
    } else {
      vec2 toL = L.posRadius.xy - logical;
      float dist = length(toL);
      float atten = clamp(1.0 - dist / max(L.posRadius.w, 1.0), 0.0, 1.0);
      atten *= atten;
      if (L.spot.z > -1.5) {
        vec2 fromL = logical - L.posRadius.xy;
        float fl = length(fromL);
        float spotAtten = 1.0;
        if (fl > 1e-4) {
          vec2 beam = L.spot.xy;
          float beamLen = length(beam);
          if (beamLen > 1e-6)
            spotAtten = smoothstep(L.spot.z, L.spot.w, dot(fromL / fl, beam / beamLen));
          else
            spotAtten = 0.0;
        }
        atten *= spotAtten;
      }
      vec3 Ld = normalize(vec3(toL, L.posRadius.w * 0.35));
      contrib = max(dot(N, Ld), 0.0) * atten;
    }
    lit += lightCol * contrib;
  }

  outColor = vec4(base.rgb * lit, base.a);
}
