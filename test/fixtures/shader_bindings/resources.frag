#version 450
layout(location=0) out vec4 outColor;
layout(set=0,binding=0,std140) uniform Frame { mat4 mvp; mat4 model; } frame;
layout(set=1,binding=32,std140) uniform Params { vec4 color; } params;
layout(set=1,binding=3) uniform sampler2D albedo;
void main() { outColor = params.color * frame.model[0][0] + texture(albedo,vec2(.5)); }
