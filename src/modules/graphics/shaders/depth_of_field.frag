#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D DepthTex;
layout(push_constant) uniform Externals { float data[32]; }
u;
void main() {
    vec2  texel    = vec2(u.data[0], u.data[1]);
    float distance = mix(u.data[5], u.data[6], texture(DepthTex, fragUV).r);
    float coc      = clamp(abs(distance - u.data[2]) / max(u.data[3], 0.0001), 0.0, 1.0);
    float radius   = coc * max(u.data[4], 0.0);
    vec2  r        = texel * radius;
    vec4  c        = texture(MainTex, fragUV) * 0.2;
    c += (texture(MainTex, fragUV + vec2(r.x, 0)) + texture(MainTex, fragUV - vec2(r.x, 0)) +
          texture(MainTex, fragUV + vec2(0, r.y)) + texture(MainTex, fragUV - vec2(0, r.y))) *
         0.1;
    c += (texture(MainTex, fragUV + r) + texture(MainTex, fragUV - r) + texture(MainTex, fragUV + vec2(r.x, -r.y)) +
          texture(MainTex, fragUV + vec2(-r.x, r.y))) *
         0.1;
    outColor = vec4(c.rgb * fragColor.rgb, texture(MainTex, fragUV).a);
}
