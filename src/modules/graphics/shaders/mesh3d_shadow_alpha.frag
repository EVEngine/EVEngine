#version 450
// Alpha-tested shadow caster fragment: discards transparent texels so
// billboard/card geometry (sprite-stack slices) casts silhouette shadows.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Push { mat4 mvp; vec4 lodFade; } pc;

void main() {
    if (texture(MainTex, vUV).a < 0.05) discard;
    if (pc.lodFade.z > 0.5) {
        float h = fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715)));
        bool rejected = pc.lodFade.y < 0.5 ? h >= pc.lodFade.x : h < 1.0 - pc.lodFade.x;
        if (rejected) discard;
    }
}
