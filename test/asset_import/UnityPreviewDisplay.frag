#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D texSampler;
void main() {
    vec4 sampleColor = texture(texSampler, fragUV);
    vec3 linearColor = max(sampleColor.rgb, vec3(0.0));
    vec3 encoded = mix(1.055 * pow(linearColor, vec3(1.0 / 2.4)) - 0.055,
                       12.92 * linearColor, lessThanEqual(linearColor, vec3(0.0031308)));
    outColor = vec4(encoded, sampleColor.a) * fragColor;
}
