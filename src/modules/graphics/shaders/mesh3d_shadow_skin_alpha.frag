#version 450

layout(location = 0) in vec2 vUV;
layout(set = 0, binding = 1) uniform sampler2D MainTex;

void main() {
    if (texture(MainTex, vUV).a < 0.05) discard;
}
