#version 450
layout(location=0) in vec4 color;
layout(location=1) in vec2 uv;
layout(location=0) out vec4 outputColor;
layout(set=0,binding=0) uniform sampler2D source;
void main(){
 vec3 linear=texture(source,uv).rgb;
 vec3 srgb=mix(12.92*linear,1.055*pow(max(linear,vec3(0)),vec3(1.0/2.4))-.055,step(vec3(.0031308),linear));
 outputColor=vec4(srgb,1.0)*color;
}
