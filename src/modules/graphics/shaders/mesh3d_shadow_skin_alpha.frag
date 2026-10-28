#version 450

layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 1) uniform sampler2D MainTex;
layout(set = 0, binding = 0, std140) uniform SkinPass {
    mat4 mvp; mat4 model; vec4 lodFade; vec4 skinInfo;
} pc;

void main() {
    if (texture(MainTex, vUV).a < 0.05) discard;
    if (pc.lodFade.z > 0.5) {
        float h = fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715)));
        bool rejected = pc.lodFade.y < 0.5 ? h >= pc.lodFade.x : h < 1.0 - pc.lodFade.x;
        if (rejected) discard;
    }
}
