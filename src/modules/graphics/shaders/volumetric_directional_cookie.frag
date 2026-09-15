#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
layout(push_constant) uniform Externals { float data[32]; } u;

mat4 loadInvVP() {
  return mat4(u.data[0],u.data[1],u.data[2],u.data[3],u.data[4],u.data[5],u.data[6],u.data[7],
              u.data[8],u.data[9],u.data[10],u.data[11],u.data[12],u.data[13],u.data[14],u.data[15]);
}

void main() {
  float depth = texture(DepthTex, fragUV).r;
  if (depth <= 1e-5 || depth >= 0.99999) { outColor = vec4(0.0); return; }
  vec4 worldH = loadInvVP() * vec4(fragUV * 2.0 - 1.0, depth, 1.0);
  vec3 world = worldH.xyz / max(worldH.w, 1e-6);
  vec3 lightDir = normalize(vec3(u.data[18],u.data[19],u.data[20]) + vec3(1e-6));
  vec3 helper = abs(lightDir.y) < 0.99 ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0);
  vec3 axisU = normalize(cross(helper, lightDir));
  vec3 axisV = normalize(cross(lightDir, axisU));
  float size = max(u.data[16], 1e-3);
  vec2 cookieUV = fract(vec2(dot(world, axisU), dot(world, axisV)) / size + 0.5);
  float ridgeA = 1.0 - abs(sin((cookieUV.x + 0.28 * sin(cookieUV.y * 12.0)) * 19.0));
  float ridgeB = 1.0 - abs(sin((cookieUV.y + 0.24 * sin(cookieUV.x * 15.0)) * 23.0));
  float procedural = 0.12 + 0.88 * pow(max(ridgeA, ridgeB), 5.0);
  vec3 cookie = max(texture(MainTex, cookieUV).rgb, vec3(procedural));
  float alpha = clamp(max(max(cookie.r, cookie.g), cookie.b) * max(u.data[17], 0.0), 0.0, 1.0);
  outColor = vec4(cookie * fragColor.rgb, alpha * fragColor.a);
}
