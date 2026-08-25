#version 450
// Binding 0: linear scene depth [0,1]. Binding 1: integrated froxel slice atlas.
// Atlas RGB is Reinhard-encoded in-scattering; alpha is transmittance.
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D VolumeTex;
layout(push_constant) uniform Externals { float data[5]; } u;

vec4 sampleSlice(float slice) {
  float cols = max(u.data[0], 1.0);
  float rows = max(u.data[1], 1.0);
  float z = clamp(slice, 0.0, max(u.data[2] - 1.0, 0.0));
  vec2 tile = vec2(mod(z, cols), floor(z / cols));
  vec2 uv = (tile + fragUV) / vec2(cols, rows);
  return texture(VolumeTex, uv);
}

void main() {
  float depth01 = clamp(texture(MainTex, fragUV).r, 0.0, 1.0);
  float nearD = max(u.data[3], 1e-3);
  float farD = max(u.data[4], nearD + 1e-3);
  float distance = mix(nearD, farD, depth01);
  float zf = log(max(distance, nearD) / nearD) / log(farD / nearD) * u.data[2] - 0.5;
  float z0 = floor(max(zf, 0.0));
  float z1 = min(z0 + 1.0, u.data[2] - 1.0);
  vec4 packed = mix(sampleSlice(z0), sampleSlice(z1), fract(max(zf, 0.0)));
  vec3 radiance = packed.rgb / max(vec3(1.0) - packed.rgb, vec3(1e-3));
  float opacity = clamp(1.0 - packed.a, 0.0, 1.0);
  outColor = vec4(radiance * fragColor.rgb, opacity);
}
