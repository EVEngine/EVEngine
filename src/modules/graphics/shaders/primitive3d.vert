#version 450

layout(location = 0) in vec4 inClipPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = inClipPosition;
    fragColor = inColor;
}
