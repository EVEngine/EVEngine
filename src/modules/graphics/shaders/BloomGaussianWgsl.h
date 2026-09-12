#pragma once
namespace eve::graphics::shaders {
/** @brief Gaussian-scatter HDR bloom stages. @lifetime Static process storage. */
inline constexpr const char* kBloomGaussian = R"wgsl(
fn gaussian_linear(image:texture_2d<f32>,uv:vec2f)->vec3f {
 let size=vec2i(textureDimensions(image));let pos=uv*vec2f(size)-.5;let lo=vec2i(floor(pos));let f=fract(pos);
 let a=textureLoad(image,clamp(lo,vec2i(0),size-1),0).rgb;
 let b=textureLoad(image,clamp(lo+vec2i(1,0),vec2i(0),size-1),0).rgb;
 let c=textureLoad(image,clamp(lo+vec2i(0,1),vec2i(0),size-1),0).rgb;
 let d=textureLoad(image,clamp(lo+vec2i(1,1),vec2i(0),size-1),0).rgb;
 return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
fn gaussian_sample(uv:vec2f,h:vec2f)->vec3f{return gaussian_linear(mainTex,clamp(uv,.5*vec2f(p(0),p(1)),vec2f(1)-h));}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f {
 let t=vec2f(p(0),p(1)); var c=vec3f(0);
 if(p(2)<.5){
  c=min(gaussian_sample(i.uv,t*.5),vec3f(p(4)));
  let brightness=max(c.r,max(c.g,c.b));let knee=p(3)*.5;
  var soft=clamp(brightness-p(3)+knee,0,2*knee);soft=soft*soft/(4*knee+.0001);
  c=max(c*(max(brightness-p(3),soft)/max(brightness,.0001)),vec3f(0));
 }else if(p(2)<1.5){
  let weights=array<f32,5>(.22702703,.19459459,.12162162,.05405405,.01621622);
  for(var j:i32=-4;j<=4;j++){c+=gaussian_sample(i.uv+vec2f(f32(j)*2*t.x,0),t)*weights[abs(j)];}
 }else if(p(2)<2.5){
  c=gaussian_sample(i.uv,t*.5)*.22702703;
  c+=(gaussian_sample(i.uv+vec2f(0,1.38461538*t.y),t*.5)+gaussian_sample(i.uv-vec2f(0,1.38461538*t.y),t*.5))*.31621622;
  c+=(gaussian_sample(i.uv+vec2f(0,3.23076923*t.y),t*.5)+gaussian_sample(i.uv-vec2f(0,3.23076923*t.y),t*.5))*.07027027;
 }else{
  let h=.5/vec2f(textureDimensions(auxTex));
  let high=gaussian_sample(i.uv,t*.5);let low=gaussian_linear(auxTex,clamp(i.uv,h,vec2f(1)-h));
  c=select(high+low*p(6),mix(high,low,p(5)),p(2)<3.5);
 }
 return vec4f(c*i.color.rgb,1);
}
)wgsl";
}  // namespace eve::graphics::shaders
