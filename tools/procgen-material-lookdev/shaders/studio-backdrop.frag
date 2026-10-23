#version 450
layout(location=3) in vec3 vWorldPos;
layout(location=0) out vec4 outColor;
void main(){
 // A real opaque backplate shared by reflected/transmitted scene comparisons.
 float value=.035+.012*clamp(vWorldPos.y*.08+.5,0,1);
 float dither=fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5;
 value+=dither/255.0;
 outColor=vec4(vec3(value),1);
}
