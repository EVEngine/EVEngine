#version 450

layout(set = 0, binding = 0) uniform sampler2D particleTexture;
layout(set = 0, binding = 2) uniform sampler2D sceneDepthTexture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) in vec2 fragSceneUv;
layout(location = 3) flat in vec3 fragSoft;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(particleTexture, fragUv) * fragColor;
    if (fragSoft.x > 0.5) {
        float sceneDepth = texture(sceneDepthTexture, clamp(fragSceneUv, vec2(0.0), vec2(1.0))).r;
        float fade = clamp((sceneDepth - fragSoft.y) / max(fragSoft.z, 1e-5), 0.0, 1.0);
        outColor.a *= fade;
    }
    if (outColor.a <= 0.001) discard;
}
