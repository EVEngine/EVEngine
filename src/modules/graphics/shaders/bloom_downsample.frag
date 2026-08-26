#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luminance(vec3 color) { return dot(color, vec3(0.2126, 0.7152, 0.0722)); }

vec3 prefilter(vec3 color) {
  color = max(color, vec3(0.0));
  float threshold = max(u.data[3], 0.0);
  float knee = max(u.data[4], 1e-4);
  float brightness = max(color.r, max(color.g, color.b));
  float soft = clamp((brightness - threshold + knee) / (2.0 * knee), 0.0, 1.0);
  float contribution = max(brightness - threshold, 0.0) + soft * soft * knee;
  return color * (contribution / max(brightness, 1e-4));
}

vec3 sampleColor(vec2 uv) {
  vec3 color = texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
  if (u.data[2] > 0.5) color = prefilter(color);
  return color;
}

vec3 karisAverage(vec3 a, vec3 b, vec3 c, vec3 d) {
  float wa = 1.0 / (1.0 + luminance(a));
  float wb = 1.0 / (1.0 + luminance(b));
  float wc = 1.0 / (1.0 + luminance(c));
  float wd = 1.0 / (1.0 + luminance(d));
  return (a * wa + b * wb + c * wc + d * wd) / max(wa + wb + wc + wd, 1e-4);
}

void main() {
  vec2 t = vec2(u.data[0], u.data[1]);
  vec3 center = sampleColor(fragUV);
  vec3 inner = karisAverage(sampleColor(fragUV + t * vec2(-1.0, -1.0)),
                            sampleColor(fragUV + t * vec2(1.0, -1.0)),
                            sampleColor(fragUV + t * vec2(-1.0, 1.0)),
                            sampleColor(fragUV + t * vec2(1.0, 1.0)));
  vec3 outer = karisAverage(sampleColor(fragUV + t * vec2(-2.0, -2.0)),
                            sampleColor(fragUV + t * vec2(2.0, -2.0)),
                            sampleColor(fragUV + t * vec2(-2.0, 2.0)),
                            sampleColor(fragUV + t * vec2(2.0, 2.0)));
  vec3 axial = karisAverage(sampleColor(fragUV + t * vec2(-2.0, 0.0)),
                            sampleColor(fragUV + t * vec2(2.0, 0.0)),
                            sampleColor(fragUV + t * vec2(0.0, -2.0)),
                            sampleColor(fragUV + t * vec2(0.0, 2.0)));
  outColor = vec4((center * 0.25 + inner * 0.5 + (outer + axial) * 0.125) * fragColor.rgb,
                  1.0);
}
