#pragma once
namespace eve::graphics::shaders {
/** @brief Hair fragment shader WGSL source.
 * @ownership Borrowed immutable string literal; callers must not free it.
 * @lifetime Static storage duration, valid for the entire process lifetime.
 * @thread Safe for concurrent reads.
 */
inline constexpr const char *kHairFragWgsl=R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f};struct Params{data:array<vec4f,8>};
struct FSIn{@location(0)vNormal:vec3f,@location(1)vUV:vec2f,@location(2)vTint:vec4f,@location(3)vWorldPos:vec3f,@location(4)vCameraPos:vec3f,@location(5)vViewPos:vec3f,@builtin(position)fragCoord:vec4f};
@group(0)@binding(0)var<uniform>ubo:Frame;@group(0)@binding(1)var albedo:texture_2d<f32>;@group(0)@binding(7)var samp:sampler;@group(0)@binding(15)var<uniform>hp:Params;fn p(i:u32)->f32{return hp.data[i/4u][i%4u];}
fn kajiya(T:vec3f,L:vec3f,V:vec3f,e:f32)->f32{let tl=dot(T,L);let tv=dot(T,V);return pow(max(sqrt(max(1.-tl*tl,0.))*sqrt(max(1.-tv*tv,0.))+tl*tv,0.),e);}
fn saturate(x:f32)->f32{return clamp(x,0.,1.);}
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let base=textureSample(albedo,samp,i.vUV)*i.vTint;if base.a<p(4){discard;}let n=normalize(i.vNormal);let v=normalize(i.vCameraPos-i.vWorldPos);let l=normalize(ubo.lightDir.xyz);var t=vec3f(p(6),p(7),p(8));if length(t)<.001{t=normalize(cross(n,vec3f(.001,1,0)));}else{t=normalize(t-n*dot(t,n));}let a=normalize(t+n*p(2));let b=normalize(t+n*p(3));let c=normalize(t-n*p(2)*.5);let lobeR=max(p(9),0.);let lobeTT=max(p(10),0.);let lobeTRT=max(p(11),0.);let selfStr=saturate(p(12));let selfBias=saturate(p(13));let rootAoStr=saturate(p(14));let r=kajiya(a,l,v,max(p(0),4.))*lobeR;let tt=kajiya(b,l,v,max(p(0)*.65,1.))*0.45*lobeTT;let trt=kajiya(c,-l,v,max(p(0)*.35,1.))*0.35*lobeTRT;let diffuse=max(dot(n,l),0.)*.65+.35;let rim=pow(1.-max(dot(n,v),0.),2.)*max(p(5),0.);let fiber=mix(1.,smoothstep(-selfBias,1.-selfBias,dot(n,l)),selfStr);let rootAo=mix(1.,pow(saturate(i.vUV.y),1.25),rootAoStr);let shadow=saturate(fiber*rootAo);let rgb=(base.rgb*(ubo.ambient.rgb+ubo.lightColor.rgb*diffuse)+ubo.lightColor.rgb*((r+tt+trt)*max(p(1),0.)+rim))*shadow;return vec4f(rgb,base.a);}
)wgsl";
}  // namespace eve::graphics::shaders
