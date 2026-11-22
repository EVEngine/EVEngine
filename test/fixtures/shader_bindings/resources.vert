#version 450
layout(location=0) in vec3 position;
layout(set=0,binding=0,std140) uniform Frame { mat4 mvp; } frame;
layout(set=1,binding=32,std140) uniform Params { vec4 color; } params;
void main() { gl_Position = frame.mvp * vec4(position + params.color.xyz, 1); }
