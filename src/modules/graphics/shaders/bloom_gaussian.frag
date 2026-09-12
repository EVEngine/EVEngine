#version 450
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D MainTex;
layout(binding=1) uniform sampler2D LowTex;
layout(push_constant) uniform Externals { float data[32]; } u;
vec3 linearSample(sampler2D image,vec2 uv) {
 ivec2 size=textureSize(image,0);vec2 pos=uv*vec2(size)-.5;
 ivec2 lo=ivec2(floor(pos));vec2 f=fract(pos);
 vec3 a=texelFetch(image,clamp(lo,ivec2(0),size-1),0).rgb;
 vec3 b=texelFetch(image,clamp(lo+ivec2(1,0),ivec2(0),size-1),0).rgb;
 vec3 c=texelFetch(image,clamp(lo+ivec2(0,1),ivec2(0),size-1),0).rgb;
 vec3 d=texelFetch(image,clamp(lo+ivec2(1,1),ivec2(0),size-1),0).rgb;
 return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
vec3 sampleAt(vec2 uv,vec2 halfTexel) { return linearSample(MainTex,clamp(uv,.5*vec2(u.data[0],u.data[1]),vec2(1)-halfTexel)); }
void main() {
 vec2 texel=vec2(u.data[0],u.data[1]);
 vec3 color=vec3(0);
 if(u.data[2]<0.5) {
  color=min(sampleAt(fragUV,texel*.5),vec3(u.data[4]));
  float brightness=max(color.r,max(color.g,color.b));
  float knee=u.data[3]*.5;
  float soft=clamp(brightness-u.data[3]+knee,0,2*knee);
  soft=soft*soft/(4*knee+1e-4);
  color=max(color*(max(brightness-u.data[3],soft)/max(brightness,1e-4)),vec3(0));
 } else if(u.data[2]<1.5) {
  float weights[5]=float[5](.22702703,.19459459,.12162162,.05405405,.01621622);
  for(int j=-4;j<=4;++j) color+=sampleAt(fragUV+vec2(float(j)*2*texel.x,0),texel)*weights[abs(j)];
 } else if(u.data[2]<2.5) {
  color=sampleAt(fragUV,texel*.5)*.22702703;
  color+=(sampleAt(fragUV+vec2(0,1.38461538*texel.y),texel*.5)+sampleAt(fragUV-vec2(0,1.38461538*texel.y),texel*.5))*.31621622;
  color+=(sampleAt(fragUV+vec2(0,3.23076923*texel.y),texel*.5)+sampleAt(fragUV-vec2(0,3.23076923*texel.y),texel*.5))*.07027027;
 } else {
  vec2 lowHalf=.5/vec2(textureSize(LowTex,0));
  vec3 high=sampleAt(fragUV,texel*.5),low=linearSample(LowTex,clamp(fragUV,lowHalf,vec2(1)-lowHalf));
  color=u.data[2]<3.5?mix(high,low,u.data[5]):high+low*u.data[6];
 }
 outColor=vec4(color*fragColor.rgb,1);
}
