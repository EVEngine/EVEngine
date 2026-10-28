#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D MaskTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
    vec4 color = texture(MainTex, fragUV) * fragColor;
    vec4 maskSample = texture(MaskTex, fragUV);
    float maskValue = maskSample.r;
    if (u.data[2] > 0.5) maskValue = 1.0 - maskValue;
    float width = max(u.data[1], 0.0001);
    float coverage = smoothstep(u.data[0] - width, u.data[0] + width, maskValue);
    outColor = vec4(color.rgb, color.a * coverage);
}
