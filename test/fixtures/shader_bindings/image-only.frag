#version 450
layout(location=0) out vec4 outColor;
layout(set=1,binding=3) uniform sampler2D albedo;
void main() { outColor = texture(albedo,vec2(.5)); }
