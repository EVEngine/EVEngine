#version 450
layout(location=0) in vec3 position;
layout(set=0,binding=0,std140) uniform Frame { mat4 mvp; } frame;
layout(location=0) out vec4 screenClip;
void main() {
    gl_Position=frame.mvp*vec4(position,1.0);
    screenClip=gl_Position;
}
