#version 450
// Alpha-tested shadow caster fragment: discards transparent texels so
// billboard/card geometry (sprite-stack slices) casts silhouette shadows.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D MainTex;

void main() {
    if (texture(MainTex, vUV).a < 0.05) discard;
}
