#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D ExposureTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  vec2 centered = fragUV - vec2(0.5);
  vec2 sampleUV = fragUV;
  float lensIntensity = clamp(u.data[6], 0.0, 1.0) * 100.0;
  if (lensIntensity > 0.0001) {
    sampleUV = centered / clamp(u.data[17], 0.01, 5.0) + vec2(0.5);
    vec2 ruv = sampleUV - vec2(0.5);
    float ru = length(ruv);
    float theta = radians(min(160.0, 1.6 * max(lensIntensity, 1.0)));
    float sigma = 2.0 * tan(theta * 0.5);
    float factor = ru > 0.000001 ? tan(ru * theta) / (ru * sigma) : theta / sigma;
    sampleUV += ruv * (factor - 1.0);
  }
  sampleUV = clamp(sampleUV, vec2(0.001), vec2(0.999));
  vec4 hdr = texture(MainTex, sampleUV);
  vec3 lift = vec3(u.data[7], u.data[8], u.data[9]);
  vec3 inverseGamma = max(vec3(u.data[10], u.data[11], u.data[12]), vec3(0.001));
  vec3 gain = vec3(u.data[13], u.data[14], u.data[15]);
  vec3 graded = hdr.rgb * gain + lift;
  graded = sign(graded) * pow(abs(graded), inverseGamma);
  float automatic = u.data[1] > 0.5 ? texture(ExposureTex, vec2(0.5)).r : 1.0;
  vec3 colorFilter = max(vec3(u.data[2], u.data[3], u.data[4]), vec3(0.0));
  vec2 vignetteDelta = abs(sampleUV - vec2(0.5)) * clamp(u.data[5], 0.0, 1.0) * 3.0;
  vignetteDelta.x *= float(textureSize(MainTex, 0).x) / float(textureSize(MainTex, 0).y);
  float vignette = pow(clamp(1.0 - dot(vignetteDelta, vignetteDelta), 0.0, 1.0),
                       clamp(u.data[16], 0.01, 1.0) * 5.0);
  outColor = vec4(graded * max(u.data[0], 0.0) * automatic * colorFilter * vignette, hdr.a);
}
