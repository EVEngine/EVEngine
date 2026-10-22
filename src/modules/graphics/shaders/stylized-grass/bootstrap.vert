#version 450
layout(location=0) in vec3 position;
layout(set=0,binding=0,std140) uniform Frame { mat4 mvp; } frame;
void main() { gl_Position=frame.mvp*vec4(position,1.0); }
