#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kScreenEffectCommon = R"wgsl(
struct FSIn { @location(0) color: vec4f, @location(1) uv: vec2f };
struct Externals { data: array<vec4f,8> };
@group(0) @binding(0) var mainTex:texture_2d<f32>;
@group(0) @binding(1) var depthTex:texture_2d<f32>;
@group(0) @binding(2) var samp:sampler;
@group(0) @binding(4) var<uniform> u:Externals;
fn p(i:u32)->f32{return u.data[i/4u][i%4u];}
fn invvp()->mat4x4f{return mat4x4f(u.data[0],u.data[1],u.data[2],u.data[3]);}
fn color_at(uv:vec2f)->vec3f{return textureSampleLevel(mainTex,samp,clamp(uv,vec2f(0),vec2f(1)),0).rgb;}
fn depth_at(uv:vec2f)->f32{return textureSampleLevel(depthTex,samp,clamp(uv,vec2f(0),vec2f(1)),0).r;}
fn world(m:mat4x4f,uv:vec2f,z:f32)->vec3f{let h=m*vec4f(uv*2-1,clamp(z,0,1),1);return h.xyz/max(h.w,.000001);}
)wgsl";

inline constexpr const char *kSsgi = R"wgsl(
fn gi_depth(uv:vec2f,use:f32)->f32{if use>.5{return depth_at(uv);}return textureSampleLevel(mainTex,samp,clamp(uv,vec2f(0),vec2f(1)),0).a;}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let m=invvp();let radius=max(p(18),.0001);let intensity=max(p(19),0);let count=i32(clamp(p(20),4,24));let tw=max(p(27),.00001);let th=max(p(28),.00001);let use=p(29);let z=gi_depth(i.uv,use);
 if z>=select(.999,.9999,use>.5)||intensity<.0001{return vec4f(0);}let pos=world(m,i.uv,z);let eh=m*vec4f(0,0,0,1);let cam=eh.xyz/max(eh.w,.000001);
 let dr=gi_depth(i.uv+vec2f(tw,0),use);let du=gi_depth(i.uv+vec2f(0,th),use);var n=normalize(cross(world(m,i.uv+vec2f(tw,0),dr)-pos,world(m,i.uv+vec2f(0,th),du)-pos));if dot(n,cam-pos)<0{n=-n;}
 let up=select(vec3f(0,1,0),vec3f(1,0,0),abs(n.y)>.999);let tangent=normalize(up-n*dot(up,n));let bit=cross(n,tangent);var bounce=vec3f(0);var weight=0.0;
 for(var q=1;q<=24;q++){if q>count{break;}let f=f32(q);let a=f*2.39996323;let rr=sqrt(f/f32(count));let dir=normalize(tangent*cos(a)+bit*sin(a)+n*.55);let suv=clamp(i.uv+dir.xy*radius*.08*rr,vec2f(0),vec2f(1));let sz=gi_depth(suv,use);if abs(sz-z)<.35{let w=max(dot(n,dir),0)*(1-rr*.6);bounce+=color_at(suv)*w;weight+=w;}}
 if weight<.0001{return vec4f(0);}bounce=bounce/weight*intensity;let alpha=clamp(max(bounce.r,max(bounce.g,bounce.b)),0,.14);return vec4f(bounce/max(alpha,.0001),alpha)*i.color;}
)wgsl";

inline constexpr const char *kSsr = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{if p(28)<.5{return vec4f(0);}let z=depth_at(i.uv);if z>.999{return vec4f(0);}let m=invvp();let vp=inverse(m);let pos=world(m,i.uv,z);let tw=max(p(21),.00001);let th=max(p(22),.00001);
 var n=normalize(cross(world(m,i.uv+vec2f(tw,0),depth_at(i.uv+vec2f(tw,0)))-pos,world(m,i.uv+vec2f(0,th),depth_at(i.uv+vec2f(0,th)))-pos));let cam=vec3f(p(16),p(17),p(18));if dot(n,cam-pos)<0{n=-n;}let v=normalize(cam-pos);let r=reflect(-v,n);if dot(r,v)>.1{return vec4f(0);}
 let maxd=max(p(23),.01);let step=max(p(24),.001);let count=i32(clamp(p(25),1,512));var ray=step;
 for(var q=0;q<512;q++){if q>=count||ray>maxd{break;}let clip=vp*vec4f(pos+r*ray,1);let ndc=clip.xyz/max(clip.w,.000001);let uv=ndc.xy*.5+.5;if any(uv<vec2f(0))||any(uv>vec2f(1))||ndc.z<0||ndc.z>1{break;}if ray>p(26)&&ndc.z>depth_at(uv)-p(29){let fade=(1-smoothstep(0,maxd,ray))*max(p(27),0);return vec4f(color_at(uv)*fade,fade)*i.color;}ray+=step;}
 return vec4f(0);}
)wgsl";

}  // namespace eve::graphics::shaders
