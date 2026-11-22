#version 450
layout(location=0) out vec4 outColor;
layout(set=1,binding=32,std140) uniform Params { ivec4 color; } params;
layout(set=1,binding=3) uniform sampler2D albedo;
void main() { outColor = vec4(params.color) + texture(albedo,vec2(.5)); }
