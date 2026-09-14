#pragma once
namespace eve::graphics::shaders {
inline constexpr const char *kHairFragWgsl = R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f};struct Params{data:array<vec4f,8>};
struct FSIn{@location(0)vNormal:vec3f,@location(1)vUV:vec2f,@location(2)vTint:vec4f,@location(3)vWorldPos:vec3f,@location(4)vCameraPos:vec3f,@location(5)vViewPos:vec3f,@builtin(position)fragCoord:vec4f};
@group(0)@binding(0)var<uniform>ubo:Frame;@group(0)@binding(1)var albedo:texture_2d<f32>;@group(0)@binding(7)var samp:sampler;@group(0)@binding(15)var<uniform>hp:Params;fn p(i:u32)->f32{return hp.data[i/4u][i%4u];}
fn kk(t:vec3f,l:vec3f,v:vec3f,e:f32)->f32{let tl=dot(t,l);let tv=dot(t,v);return pow(max(sqrt(max(1.0-tl*tl,0.0))*sqrt(max(1.0-tv*tv,0.0))+tl*tv,0.0),e);}
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let base=textureSample(albedo,samp,i.vUV)*i.vTint;if base.a<p(4u){discard;}let n=normalize(i.vNormal);let v=normalize(i.vCameraPos-i.vWorldPos);let l=normalize(ubo.lightDir.xyz);var t=vec3f(p(6u),p(7u),p(8u));if length(t)<0.001{t=normalize(cross(n,vec3f(0.001,1.0,0.0)));}else{t=normalize(t-n*dot(t,n));}let t1=normalize(t+n*p(2u));let t2=normalize(t+n*p(3u));let s1=kk(t1,l,v,max(p(0u),4.0));let s2=kk(t2,l,v,max(p(0u),4.0)*0.65)*0.45;let wrap=clamp(p(9u),0.0,1.0);let scatter=max(p(10u),0.0);let nd=dot(n,l);let wrapL=clamp((nd+wrap)/max(1.0+wrap,1e-3),0.0,1.0);let diffuse=base.rgb*(0.18+0.82*wrapL);let back=max(dot(-n,l),0.0);let scatterCol=base.rgb*scatter*back*back;let light=min(ubo.lightColor.rgb,vec3f(1.4));var spec=vec3f(s1+s2)*max(p(1u),0.0)*light;spec+=vec3f(1.0,0.82,0.65)*s2*0.35*light;let rim=pow(clamp(1.0-max(dot(n,v),0.0),0.0,1.0),3.0)*max(p(5u),0.0)*base.rgb;let rgb=diffuse*light+scatterCol*light+spec+rim;return vec4f(clamp(rgb,vec3f(0.0),vec3f(1.0)),base.a);}

)wgsl";
}  // namespace eve::graphics::shaders
