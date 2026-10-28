#version 450

layout(set = 0, binding = 0) uniform sampler2D SceneColor;
layout(set = 0, binding = 1) uniform sampler2D Displacement;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 field = texture(Displacement, fragUV);
    vec2 sceneSize = vec2(textureSize(SceneColor, 0));
    vec2 sceneUV = gl_FragCoord.xy / sceneSize;
    vec2 offsetUV = (field.rg * 2.0 - 1.0) * (fragColor.r / sceneSize);
    vec3 refracted = texture(SceneColor, clamp(sceneUV + offsetUV, vec2(0.0), vec2(1.0))).rgb;
    outColor = vec4(refracted, clamp(field.a * fragColor.a, 0.0, 1.0));
}
