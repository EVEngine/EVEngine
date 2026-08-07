#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec2 fragNdc;

layout(set = 0, binding = 0) uniform sampler2D albedoSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;

struct Light2D {
  vec4 posRadius; // xy = point pos OR direction; w = radius (0 => directional)
  vec4 color;     // rgb * intensity
};

layout(set = 0, binding = 2) uniform Lighting2D {
  vec4 ambient;   // rgb
  vec4 meta;      // x = lightCount, y = viewW, z = viewH
  Light2D lights[8];
} lighting;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 base = texture(albedoSampler, fragUV) * fragColor;
  vec3 nSample = texture(normalSampler, fragUV).xyz * 2.0 - 1.0;
  // 2D convention: tangent ≈ screen X/Y, Z out of screen.
  vec3 N = normalize(vec3(nSample.xy, max(nSample.z, 0.05)));

  vec2 logical = (fragNdc * 0.5 + 0.5) * lighting.meta.yz;

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
      vec3 Ld = normalize(vec3(toL, L.posRadius.w * 0.35));
      contrib = max(dot(N, Ld), 0.0) * atten;
    }
    lit += lightCol * contrib;
  }

  outColor = vec4(base.rgb * lit, base.a);
}
