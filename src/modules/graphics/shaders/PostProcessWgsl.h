#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kPostCommon = R"wgsl(
struct FSIn { @location(0) color: vec4f, @location(1) uv: vec2f };
struct Externals { data: array<vec4f, 8> };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(1) var auxTex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(4) var<uniform> u: Externals;
fn p(i:u32)->f32{return u.data[i/4u][i%4u];}
fn tex(uv:vec2f)->vec4f{return textureSampleLevel(mainTex,samp,clamp(uv,vec2f(0),vec2f(1)),0);}
fn luma(c:vec3f)->f32{return dot(c,vec3f(.299,.587,.114));}
fn aux_load(uv:vec2f)->vec4f{let d=textureDimensions(auxTex);let q=vec2i(clamp(uv,vec2f(0),vec2f(1))*vec2f(d));return textureLoad(auxTex,clamp(q,vec2i(0),vec2i(d)-1),0);}
)wgsl";

inline constexpr const char *kFxaa = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{
 let t=vec2f(p(0),p(1));let c=tex(i.uv);let lm=luma(c.rgb);let n=luma(tex(i.uv-vec2f(0,t.y)).rgb);let s=luma(tex(i.uv+vec2f(0,t.y)).rgb);
 let e=luma(tex(i.uv+vec2f(t.x,0)).rgb);let w=luma(tex(i.uv-vec2f(t.x,0)).rgb);let lo=min(lm,min(min(n,s),min(e,w)));let hi=max(lm,max(max(n,s),max(e,w)));
 let range=hi-lo;if range<max(max(p(3),0),hi*max(p(2),.0001)){return vec4f(c.rgb*i.color.rgb,1);}
 let gx=e-w;let gy=s-n;let tangent=normalize(vec2f(-gy,gx)+vec2f(.000001));let amount=clamp(range/max(hi,.00001)*clamp(p(4),0,1),0,.75);
 let a=tex(i.uv-tangent*t*.75).rgb;let b=tex(i.uv+tangent*t*.75).rgb;return vec4f(mix(c.rgb,(a+b)*.5,amount)*i.color.rgb,1);
}
)wgsl";

inline constexpr const char *kNfaa = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let t=vec2f(p(0),p(1));let c=tex(i.uv).rgb;
 let g=vec2f(luma(tex(i.uv+vec2f(t.x,0)).rgb)-luma(tex(i.uv-vec2f(t.x,0)).rgb),luma(tex(i.uv+vec2f(0,t.y)).rgb)-luma(tex(i.uv-vec2f(0,t.y)).rgb));
 let gl=length(g);if gl<.00001{return vec4f(c*i.color.rgb,1);}let tangent=vec2f(-g.y,g.x)/gl;let amt=clamp(p(2),0,2)*pow(clamp(gl*4,0,1),max(p(3),.1));let o=tangent*t*clamp(p(4),.5,2.5)*amt;
 let f=(tex(i.uv-o*1.5).rgb+2*tex(i.uv-o*.5).rgb+2*c+2*tex(i.uv+o*.5).rgb+tex(i.uv+o*1.5).rgb)/8;
 return vec4f(mix(c,f,clamp(amt,0,1))*i.color.rgb,1);}
)wgsl";

inline constexpr const char *kSmaa = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let t=vec2f(p(0),p(1));let c=tex(i.uv).rgb;let m=luma(c);
 let dl=abs(m-luma(tex(i.uv-vec2f(t.x,0)).rgb));let dt=abs(m-luma(tex(i.uv-vec2f(0,t.y)).rgb));let dr=abs(m-luma(tex(i.uv+vec2f(t.x,0)).rgb));let db=abs(m-luma(tex(i.uv+vec2f(0,t.y)).rgb));
 let threshold=max(p(2),.0001);let mx=max(max(dl,dt),max(dr,db));var rgb=c;if dl>=threshold&&dl>=mx*max(p(3),0){rgb=(c+tex(i.uv-vec2f(t.x,0)).rgb)*.5;}
 if dt>=threshold&&dt>=mx*max(p(3),0){rgb=(rgb+tex(i.uv-vec2f(0,t.y)).rgb)*.5;}return vec4f(rgb*i.color.rgb,1);}
)wgsl";

inline constexpr const char *kSsaa = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let t=vec2f(p(0),p(1));let radius=clamp(p(4),.5,3);let kind=i32(clamp(p(3),0,2));var sum=vec3f(0);var ws=0.0;
 for(var y=-3;y<=3;y++){for(var x=-3;x<=3;x++){if abs(f32(x))>radius||abs(f32(y))>radius{continue;}let d2=f32(x*x+y*y);var w=1.0;if kind==1{w=max(1-abs(f32(x))/(radius+.0001),0)*max(1-abs(f32(y))/(radius+.0001),0);}if kind==2{let s=max(radius*.5,.35);w=exp(-d2/(2*s*s));}sum+=tex(i.uv+vec2f(f32(x)*t.x,f32(y)*t.y)).rgb*w;ws+=w;}}
 return vec4f(sum/max(ws,.0001)*i.color.rgb,1);}
)wgsl";

inline constexpr const char *kOutline = R"wgsl(
fn aux(uv:vec2f)->vec3f{let n=aux_load(uv).xyz*2-1;return select(vec3f(0,0,1),normalize(n),length(n)>.001);}
fn viewz(z:f32)->f32{let n=max(p(10),.0001);let f=max(p(11),n+.001);return n*f/max(f-z*(f-n),.000001);}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let color=vec3f(p(5),p(6),p(7));let z=tex(i.uv).r;if z>=.9999{return vec4f(color,0);}let t=vec2f(max(p(8),.00001),max(p(9),.00001))*max(p(0),.5);let nc=aux(i.uv);let dc=viewz(z);var de=0.0;var ne=0.0;
 let offsets=array<vec2f,8>(vec2f(1,0),vec2f(-1,0),vec2f(0,1),vec2f(0,-1),vec2f(.707,.707),vec2f(-.707,-.707),vec2f(-.707,.707),vec2f(.707,-.707));
 for(var q=0;q<8;q++){let uv=i.uv+offsets[q]*t;let cut=max(p(1),0)+dc*max(p(2),0);let dd=abs(dc-viewz(tex(uv).r));let soft=clamp(p(4),0,1);de=max(de,smoothstep(cut*(1-soft),cut,dd));ne=max(ne,1-clamp(dot(nc,aux(uv)),-1,1));}
 let nt=clamp(p(3),0,2);let edge=max(de,smoothstep(nt*(1-clamp(p(4),0,1)),nt+clamp(p(4),0,1),ne));return vec4f(color,clamp(edge,0,1))*i.color;}
)wgsl";

}  // namespace eve::graphics::shaders
