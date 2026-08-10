#version 450
// Screen-space volumetric light scattering (GPU Gems 3 / Mitchell) + dust/fog.
// Samples MainTex toward the light screen position (occlusion map or scene).
// Push constants (declareFloat order):
//  0 lightX, 1 lightY       — UV of light (0..1)
//  2 exposure, 3 decay
//  4 density, 5 weight
//  6 sampleCount            — floored in shader
//  7 dustAmount             — particulate scintillation
//  8 fogAmount              — soft haze toward light
//  9 fogR, 10 fogG, 11 fogB
// 12 shaftR, 13 shaftG, 14 shaftB
// 15 time
// 16 compositeMode          — 0 = shafts-only (alpha=luma), 1 = add onto scene
// 17 intensity

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
  vec2 lightPos = vec2(u.data[0], u.data[1]);
  float exposure = u.data[2];
  float decay = u.data[3];
  float density = u.data[4];
  float weight = u.data[5];
  int samples = int(clamp(u.data[6], 4.0, 128.0));
  float dustAmount = u.data[7];
  float fogAmount = u.data[8];
  vec3 fogColor = vec3(u.data[9], u.data[10], u.data[11]);
  vec3 shaftTint = vec3(u.data[12], u.data[13], u.data[14]);
  float time = u.data[15];
  float compositeMode = u.data[16];
  float intensity = u.data[17];

  vec2 uv = fragUV;
  vec2 delta = (uv - lightPos) * density / float(samples);
  vec2 coord = uv;
  float illumDecay = 1.0;
  vec3 scatter = vec3(0.0);

  for (int i = 0; i < 128; ++i) {
    if (i >= samples) break;
    coord -= delta;
    vec3 s = texture(MainTex, clamp(coord, vec2(0.0), vec2(1.0))).rgb;
    s *= illumDecay * weight;
    scatter += s;
    illumDecay *= decay;
  }

  scatter *= exposure * intensity;
  scatter *= shaftTint;

  // Dust: high-frequency scintillation stronger nearer the light axis.
  float dist = length(uv - lightPos);
  float axis = exp(-dist * 3.5);
  float n1 = hash12(uv * vec2(640.0, 360.0) + time * 17.0);
  float n2 = hash12(uv * vec2(220.0, 140.0) - time * 9.0);
  float dust = (n1 * 0.65 + n2 * 0.35);
  scatter += shaftTint * dust * dustAmount * axis * intensity;

  // Soft fog / haze falloff toward the light (participating media feel).
  float fog = fogAmount * exp(-dist * 2.2) * intensity;
  scatter += fogColor * fog;

  vec3 scene = texture(MainTex, uv).rgb * fragColor.rgb;

  if (compositeMode > 0.5) {
    // Source is the scene: add shafts onto it.
    outColor = vec4(scene + scatter, 1.0);
  } else {
    // Shafts-only: alpha from luminance so alpha-blend over a prior scene draw
    // approximates additive light shafts.
    float a = clamp(luma(scatter) * 1.35, 0.0, 1.0);
    outColor = vec4(scatter, a);
  }
}
