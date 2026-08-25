#pragma once

namespace eve::weather::shaders {

inline constexpr const char *kWeatherVertWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame { mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f };
struct Params { data: array<vec4f,8> }; struct In{@location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f};
struct Out{@builtin(position) pos:vec4f,@location(0) normal:vec3f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(4) camera:vec3f,@location(5) view:vec3f,@location(6) particle:f32};
@group(0) @binding(0) var<uniform> frame:Frame; @group(0) @binding(15) var<uniform> params:Params;
fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
fn hash12(q:vec2f)->f32{var v=fract(vec3f(q.x,q.y,q.x)*0.1031);v+=dot(v,v.yzx+vec3f(33.33));return fract((v.x+v.y)*v.z);}
@vertex fn vs_main(i:In)->Out{let phase=hash12(i.pos.xz*0.5+vec2f(floor(i.pos.y*0.7),13.0));let time=p(0u)*p(3u)*i.normal.z+phase*20.0;var base=i.pos;base.x+=frame.cameraPos.x;base.z+=frame.cameraPos.z;base.y=i.pos.y-time-floor((i.pos.y-time)/20.0)*20.0;let age=1.0-base.y/20.0;base.x+=p(1u)*age*0.075;base.z+=p(2u)*age*0.075;let sway=sin(p(0u)*1.2+phase*6.2831)*0.06*p(4u);base.x+=sway*p(1u);base.z+=sway*p(2u);let velocity=normalize(vec3f(p(1u)*0.11,-p(3u),p(2u)*0.11));let right=normalize(cross(velocity,normalize(frame.cameraPos.xyz-base)));let position=base+right*i.uv.x*p(5u)*i.normal.y-velocity*i.uv.y*p(4u)*i.normal.x;let world=frame.model*vec4f(position,1);var o:Out;o.pos=frame.mvp*vec4f(position,1);o.pos.y=-o.pos.y;o.normal=i.normal;o.uv=i.uv;o.tint=frame.tint;o.world=world.xyz;o.camera=frame.cameraPos.xyz;o.view=(frame.view*world).xyz;o.particle=phase;return o;}
)wgsl";

inline constexpr const char *kWeatherFragWgsl = R"wgsl(
struct Params{data:array<vec4f,8>};struct In{@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(4) camera:vec3f,@location(5) view:vec3f,@location(6) particle:f32};
@group(0) @binding(1) var albedo:texture_2d<f32>;@group(0) @binding(7) var samp:sampler;@group(0) @binding(15) var<uniform> params:Params;fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
@fragment fn fs_main(i:In)->@location(0) vec4f{let tex=textureSample(albedo,samp,i.uv)*i.tint;if(i.particle>p(6u)||tex.a<0.075){discard;}let fog=clamp(1.0-exp(-length(i.view)*p(10u)),0.0,1.0);let glint=0.72+0.28*pow(max(0.0,1.0-abs(i.uv.x)*2.0),2.0);return vec4f(mix(tex.rgb*glint,vec3f(p(7u),p(8u),p(9u)),fog),1);}
)wgsl";

inline constexpr const char *kBoltVertWgsl = R"wgsl(
struct Light3D{posRadius:vec4f,color:vec4f};struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f};struct In{@location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f};struct Out{@builtin(position) pos:vec4f,@location(0) normal:vec3f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(4) camera:vec3f,@location(5) view:vec3f};@group(0) @binding(0) var<uniform> frame:Frame;@vertex fn vs_main(i:In)->Out{let w=frame.model*vec4f(i.pos,1);var o:Out;o.pos=frame.mvp*vec4f(i.pos,1);o.pos.y=-o.pos.y;o.normal=i.normal;o.uv=i.uv;o.tint=frame.tint;o.world=w.xyz;o.camera=frame.cameraPos.xyz;o.view=(frame.view*w).xyz;return o;}
)wgsl";

inline constexpr const char *kBoltFragWgsl = R"wgsl(
struct Params{data:array<vec4f,8>};struct In{@builtin(position) pos:vec4f,@location(1) uv:vec2f,@location(2) tint:vec4f,@location(3) world:vec3f,@location(5) view:vec3f};@group(0) @binding(15) var<uniform> params:Params;fn p(i:u32)->f32{return params.data[i/4u][i%4u];}
@fragment fn fs_main(i:In)->@location(0) vec4f{let flash=clamp(p(11u),0.0,1.0);if(flash<=0.01){discard;}let across=abs(i.uv.x);let core=1.0-smoothstep(0.08,0.34,across);let halo=pow(max(0.0,1.0-across),2.2)*flash;let bayer=array<f32,16>(0.0,0.5,0.125,0.625,0.75,0.25,0.875,0.375,0.1875,0.6875,0.0625,0.5625,0.9375,0.4375,0.8125,0.3125);let q=vec2u(i.pos.xy)%vec2u(4);if(max(core,halo*0.72)<=bayer[q.y*4u+q.x]){discard;}let color=mix(vec3f(0.20,0.38,0.95),vec3f(0.92,0.97,1.0),core)*(0.55+0.85*flash);let fog=clamp(1.0-exp(-length(i.view)*p(10u)),0.0,1.0);return vec4f(mix(color,vec3f(p(7u),p(8u),p(9u)),fog*0.6),1);}
)wgsl";

}  // namespace eve::weather::shaders
