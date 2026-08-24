#pragma once
namespace eve::graphics::webgpu {
inline constexpr const char *kHairFragWgsl=R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f};struct Params{data:array<vec4f,8>};
struct FSIn{@location(0)vNormal:vec3f,@location(1)vUV:vec2f,@location(2)vTint:vec4f,@location(3)vWorldPos:vec3f,@location(4)vCameraPos:vec3f,@location(5)vViewPos:vec3f,@builtin(position)fragCoord:vec4f};
@group(0)@binding(0)var<uniform>ubo:Frame;@group(0)@binding(1)var albedo:texture_2d<f32>;@group(0)@binding(7)var samp:sampler;@group(0)@binding(15)var<uniform>hp:Params;fn p(i:u32)->f32{return hp.data[i/4u][i%4u];}
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let base=textureSample(albedo,samp,i.vUV)*i.vTint;if base.a<p(4){discard;}let n=normalize(i.vNormal);let v=normalize(i.vCameraPos-i.vWorldPos);let l=normalize(ubo.lightDir.xyz);var t=vec3f(p(6),p(7),p(8));if length(t)<.001{t=normalize(cross(n,vec3f(.001,1,0)));}else{t=normalize(t-n*dot(t,n));}let h=normalize(v+l);let a=normalize(t+n*p(2));let b=normalize(t+n*p(3));let s1=pow(sqrt(max(1-dot(a,h)*dot(a,h),0)),max(p(0),1))*max(p(1),0);let s2=pow(sqrt(max(1-dot(b,h)*dot(b,h),0)),max(p(0)*.5,1))*max(p(1),0)*.5;let diffuse=max(dot(n,l),0)*.65+.35;let rim=pow(1-max(dot(n,v),0),2)*max(p(5),0);let rgb=base.rgb*(ubo.ambient.rgb+ubo.lightColor.rgb*diffuse)+ubo.lightColor.rgb*(s1+s2+rim);return vec4f(rgb,base.a);}
)wgsl";
}
