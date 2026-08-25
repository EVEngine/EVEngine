#pragma once

namespace eve::stylize::shaders {
inline constexpr const char *kCommon = R"wgsl(
struct In{@location(0) color:vec4f,@location(1) uv:vec2f};struct Params{data:array<vec4f,8>};
@group(0) @binding(0)var mainTex:texture_2d<f32>;@group(0) @binding(2)var samp:sampler;@group(0) @binding(4)var<uniform> params:Params;
fn p(i:u32)->f32{return params.data[i/4u][i%4u];}fn sampleTex(uv:vec2f)->vec4f{return textureSampleLevel(mainTex,samp,clamp(uv,vec2f(0),vec2f(1)),0);}fn lum(c:vec3f)->f32{return dot(c,vec3f(.2126,.7152,.0722));}
fn objectMask(c:vec3f)->f32{let y=lum(c);let sat=length(c-vec3f(y));let bg=(1-smoothstep(.10,.26,y))*(1-smoothstep(.025,.09,sat));return clamp(1-bg,0,1);}
)wgsl";
inline constexpr const char *kCartoon = R"wgsl(
@fragment fn fs_main(i:In)->@location(0) vec4f{let src=sampleTex(i.uv)*i.color;let bands=max(p(0),2);let obj=objectMask(src.rgb);var c=src.rgb;if(obj>.05){let mapped=clamp(c/(c+vec3f(.55))*1.45,vec3f(0),vec3f(1));let y=max(lum(mapped),.0001);let band=min(max(floor(y*bands+.0001)/max(bands-1,1),.5/bands),.88);c=mapped*(band/y);let q=max(p(3),2);c=floor(c*q+.5)/q;c=mix(src.rgb,c,obj);}let t=vec2f(max(p(4),.00001),max(p(5),.00001))*max(p(8),1);let m=objectMask(src.rgb);var n=m;for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){n=min(n,objectMask(sampleTex(i.uv+vec2f(f32(x),f32(y))*t).rgb));}}let edge=smoothstep(.15,.55,m)*(1-smoothstep(.05,.45,n));let line=clamp(smoothstep(max(p(2),.01),max(p(2),.01)+max(p(7),.001),edge)*p(1),0,1);return vec4f(clamp(mix(c,vec3f(.05,.04,.07),line*obj),vec3f(0),vec3f(1)),src.a);}
)wgsl";
inline constexpr const char *kWatercolor = R"wgsl(
fn hash(q:vec2f)->f32{return fract(sin(dot(q,vec2f(12.9898,78.233)))*43758.5453);}@fragment fn fs_main(i:In)->@location(0) vec4f{let t=vec2f(max(p(6),.00001),max(p(7),.00001));let warp=(vec2f(hash(i.uv*313+p(8)),hash(i.uv*271-p(8)))-.5)*p(3)*t*5;let uv=i.uv+warp;let c=sampleTex(uv);var blur=c.rgb;blur+=sampleTex(uv+vec2f(t.x,0)*p(0)).rgb;blur+=sampleTex(uv-vec2f(t.x,0)*p(0)).rgb;blur+=sampleTex(uv+vec2f(0,t.y)*p(0)).rgb;blur+=sampleTex(uv-vec2f(0,t.y)*p(0)).rgb;blur/=5;let gx=abs(lum(sampleTex(uv+vec2f(t.x,0)).rgb)-lum(sampleTex(uv-vec2f(t.x,0)).rgb));let gy=abs(lum(sampleTex(uv+vec2f(0,t.y)).rgb)-lum(sampleTex(uv-vec2f(0,t.y)).rgb));var col=mix(c.rgb,blur,clamp(p(4),0,1));let y=lum(col);col=mix(vec3f(y),col,p(5));col*=1-clamp((gx+gy)*p(1),0,.7);let paper=(hash(i.uv*vec2f(997,991))-.5)*p(2)*.12+(hash(i.uv*vec2f(173,181))-.5)*p(9)*.08;col=clamp(col+vec3f(paper),vec3f(0),vec3f(1));return vec4f(col*i.color.rgb,c.a*i.color.a);}
)wgsl";
inline constexpr const char *kInk = R"wgsl(
@fragment fn fs_main(i:In)->@location(0) vec4f{let t=vec2f(max(p(8),.00001),max(p(9),.00001));let c=sampleTex(i.uv);let y=lum(c.rgb);let levels=max(p(1),2);let wash=floor(pow(clamp(y,0,1),max(p(0),.1))*levels)/max(levels-1,1);let gx=abs(lum(sampleTex(i.uv+vec2f(t.x,0)*p(3)).rgb)-lum(sampleTex(i.uv-vec2f(t.x,0)*p(3)).rgb));let gy=abs(lum(sampleTex(i.uv+vec2f(0,t.y)*p(3)).rgb)-lum(sampleTex(i.uv-vec2f(0,t.y)*p(3)).rgb));let edge=smoothstep(p(2),p(2)+.12,(gx+gy)*p(11));let paper=vec3f(p(4),p(5),p(6));let ink=paper*(1-wash*p(7));return vec4f(mix(ink,vec3f(.025,.02,.018),edge)*i.color.rgb,c.a*i.color.a);}
)wgsl";
inline constexpr const char *kPixel = R"wgsl(
fn bayer(q:vec2u)->f32{let m=array<f32,16>(0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);return m[(q.x%4u)+(q.y%4u)*4u]/16-.5;}@fragment fn fs_main(i:In)->@location(0) vec4f{let size=max(p(0),1);let res=vec2f(max(p(8),1),max(p(9),1))/size;let cell=floor(i.uv*res);let snapped=(cell+.5)/res;let uv=mix(i.uv,snapped,clamp(p(4),0,1));let src=sampleTex(uv);let obj=objectMask(src.rgb);var c=src.rgb;if(obj>.05){let mapped=clamp(c/(c+vec3f(.7))*1.35,vec3f(0),vec3f(1));let y=max(lum(mapped),.0001);let bands=max(p(3),1);let cel=(floor(y*bands)+.5)/bands;c=mapped*(cel/y);let steps=max(p(1),2);let d=bayer(vec2u(cell))*p(2)/steps;c=floor(clamp(c+vec3f(d),vec3f(0),vec3f(1))*steps+.0001)/max(steps-1,1);}return vec4f(clamp(c,vec3f(0),vec3f(1))*i.color.rgb,sampleTex(i.uv).a*i.color.a);}
)wgsl";
}  // namespace eve::stylize::shaders
