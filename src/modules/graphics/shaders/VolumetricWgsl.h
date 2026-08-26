#pragma once

namespace eve::graphics::shaders {
inline constexpr const char *kVolCommon=R"wgsl(
struct FSIn{@location(0) color:vec4f,@location(1) uv:vec2f};struct U{data:array<vec4f,8>};
@group(0) @binding(0)var mainTex:texture_2d<f32>;@group(0) @binding(1)var depthTex:texture_2d<f32>;@group(0) @binding(2)var samp:sampler;@group(0) @binding(4)var<uniform>u:U;
fn p(i:u32)->f32{return u.data[i/4u][i%4u];}fn tex(uv:vec2f)->vec4f{return textureSampleLevel(mainTex,samp,clamp(uv,vec2f(0),vec2f(1)),0);}
)wgsl";
inline constexpr const char *kVolScreen=R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let light=vec2f(p(0),p(1));let count=i32(clamp(p(6),1,96));let delta=(i.uv-light)*p(4)/f32(count);var uv=i.uv;var illum=vec3f(0);var decay=1.0;
for(var q=0;q<96;q++){if q>=count{break;}uv-=delta;illum+=tex(uv).rgb*decay*p(5);decay*=p(3);}let fog=vec3f(p(9),p(10),p(11))*max(p(8),0);let shaft=vec3f(p(12),p(13),p(14));let rgb=(illum*shaft*p(2)+fog)*max(p(17),0);return vec4f(rgb,clamp(max(rgb.r,max(rgb.g,rgb.b)),0,1))*i.color;}
)wgsl";
inline constexpr const char *kVolRay=R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let z=tex(i.uv).r;if z>=.999{return vec4f(0);}let d=max(p(19),0);let c=vec3f(p(20),p(21),p(22));let light=normalize(vec3f(p(16),p(17),p(18)));let phase=.35+.65*max(light.y,0);let a=clamp((1-z)*d*max(p(23),0)*(1+max(p(28),0))*.35,0,.9);return vec4f(c*phase,a)*i.color;}
)wgsl";
inline constexpr const char *kVolFog=R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let z=tex(i.uv).r;if z>=.999{return vec4f(0);}let start=max(p(29),0);let end=max(p(30),start+.001);let dist=mix(max(p(24),.001),max(p(25),1),z);let range=smoothstep(start,end,dist);let height=mix(.65,1.0,clamp(i.uv.y,0,1));let a=clamp(range*max(p(19),0)*max(p(23),0)*height,0,.95);return vec4f(vec3f(p(20),p(21),p(22)),a)*i.color;}
)wgsl";
inline constexpr const char *kVolCloud=R"wgsl(
fn hash(q:vec2f)->f32{return fract(sin(dot(q,vec2f(12.9898,78.233)))*43758.5453);}
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let uv=i.uv+vec2f(p(25),p(26))*p(19)*.0002;let n=hash(floor(uv*max(p(24),1)));let cov=clamp(p(22),0,1);let density=max(p(23),0);let a=smoothstep(1-cov,1,n)*density*.7;let light=vec3f(p(29),p(30),p(31));return vec4f(light,a)*i.color;}
)wgsl";
inline constexpr const char *kVolFroxel=R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0)vec4f{let cols=max(p(0),1);let rows=max(p(1),1);let slices=max(p(2),1);let tile=vec2f(1/cols,1/rows);var acc=vec4f(0);let count=i32(clamp(slices,1,64));for(var q=0;q<64;q++){if q>=count{break;}let f=f32(q);let cell=vec2f(f-floor(f/cols)*cols,floor(f/cols));let s=tex(cell*tile+i.uv*tile);acc.rgb+=s.rgb*(1-acc.a);acc.a+=s.a*(1-acc.a);}return acc*i.color;}
)wgsl";
} // namespace eve::graphics::shaders
