#version 450
layout(location=0) in vec3 inPosition;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
} frame;
layout(location=0) out vec4 color;
void main() {
    gl_Position=frame.mvp*vec4(inPosition,1.0);
    color=frame.tint;
}
