#version 450
// NFAA — Normal Filter Anti-Aliasing (NVIDIA / classic image-space filter).
// Builds a screen-space "normal" from luma gradients and blurs along the edge.
// Push constants (declareFloat order):
//  0 texelW, 1 texelH
//  2 strength           — blur amount along the edge
//  3 power              — gradient power / sensitivity
//  4 blurScale          — sample distance multiplier (quality)

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = vec2(u.data[0], u.data[1]);
  float strength = clamp(u.data[2], 0.0, 2.0);
  float power = max(u.data[3], 0.1);
  float blurScale = clamp(u.data[4], 0.5, 2.5);

  float lC = luma(texture(MainTex, fragUV).rgb);
  float lL = luma(texture(MainTex, fragUV + vec2(-texel.x, 0.0)).rgb);
  float lR = luma(texture(MainTex, fragUV + vec2( texel.x, 0.0)).rgb);
  float lT = luma(texture(MainTex, fragUV + vec2(0.0, -texel.y)).rgb);
  float lB = luma(texture(MainTex, fragUV + vec2(0.0,  texel.y)).rgb);

  // Screen-space normal from depth-like luma field
  vec2 grad = vec2(lR - lL, lB - lT);
  float glen = length(grad);
  if (glen < 1e-5) {
    outColor = vec4(texture(MainTex, fragUV).rgb * fragColor.rgb, 1.0);
    return;
  }
  vec2 n = grad / glen;
  // Tangent = perpendicular to gradient → blur along the edge
  vec2 t = vec2(-n.y, n.x);

  float amount = strength * pow(clamp(glen * 4.0, 0.0, 1.0), power);
  vec2 offset = t * texel * blurScale * amount;

  vec3 c0 = texture(MainTex, fragUV - offset * 1.5).rgb;
  vec3 c1 = texture(MainTex, fragUV - offset * 0.5).rgb;
  vec3 c2 = texture(MainTex, fragUV).rgb;
  vec3 c3 = texture(MainTex, fragUV + offset * 0.5).rgb;
  vec3 c4 = texture(MainTex, fragUV + offset * 1.5).rgb;

  // 5-tap tent along the edge
  vec3 rgb = (c0 + 2.0 * c1 + 2.0 * c2 + 2.0 * c3 + c4) / 8.0;
  // Preserve flat regions
  rgb = mix(c2, rgb, clamp(amount, 0.0, 1.0));

  outColor = vec4(rgb * fragColor.rgb, 1.0);
}
