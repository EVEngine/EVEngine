#pragma once

namespace eve::graphics::shaders {

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kPostCommon = R"wgsl(
struct FSIn { @location(0) color: vec4f, @location(1) uv: vec2f };
struct Externals { data: array<vec4f, 8> };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(1) var auxTex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(4) var<uniform> u: Externals;
@group(0) @binding(5) var motionTex: texture_2d<f32>;
@group(0) @binding(6) var motionSamp: sampler;
fn p(i:u32)->f32{return u.data[i/4u][i%4u];}
fn tex(uv:vec2f)->vec4f{return textureSampleLevel(mainTex,samp,clamp(uv,vec2f(0),vec2f(1)),0);}
fn luma(c:vec3f)->f32{return dot(c,vec3f(.299,.587,.114));}
fn aux_load(uv:vec2f)->vec4f{let d=textureDimensions(auxTex);let q=vec2i(clamp(uv,vec2f(0),vec2f(1))*vec2f(d));return textureLoad(auxTex,clamp(q,vec2i(0),vec2i(d)-1),0);}
fn motion_load(uv:vec2f)->vec4f{return textureSampleLevel(motionTex,motionSamp,clamp(uv,vec2f(0),vec2f(1)),0);}
fn cubic_weights(t:f32)->vec4f{let t2=t*t;let t3=t2*t;return vec4f(-.5*t+t2-.5*t3,1-2.5*t2+1.5*t3,.5*t+2*t2-1.5*t3,-.5*t2+.5*t3);}
fn aux_cubic(uv:vec2f)->vec4f{let size=vec2f(textureDimensions(auxTex));let position=uv*size-.5;let base=floor(position);let f=fract(position);let wx=cubic_weights(f.x);let wy=cubic_weights(f.y);var result=vec4f(0);for(var y=0;y<4;y++){for(var x=0;x<4;x++){let suv=(base+vec2f(f32(x-1),f32(y-1))+.5)/size;result+=textureSampleLevel(auxTex,samp,clamp(suv,vec2f(0),vec2f(1)),0)*wx[x]*wy[y];}}return result;}
fn rgb_to_ycocg(c:vec3f)->vec3f{return vec3f(c.r*.25+c.g*.5+c.b*.25,c.r*.5-c.b*.5,-c.r*.25+c.g*.5-c.b*.25);}
fn ycocg_to_rgb(c:vec3f)->vec3f{return vec3f(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
fn hdr_ycocg(c:vec3f)->vec3f{let q=rgb_to_ycocg(max(c,vec3f(0)));let r=1+max(q.x,0);return vec3f(log2(r),q.yz/r);}
fn hdr_rgb(c:vec3f)->vec3f{let y=max(exp2(c.x)-1,0);let r=1+y;return max(ycocg_to_rgb(vec3f(y,c.yz*r)),vec3f(0));}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kBloomDownsample = R"wgsl(
fn bloom_luma(c:vec3f)->f32{return dot(c,vec3f(.2126,.7152,.0722));}
fn bloom_prefilter(c0:vec3f)->vec3f{let c=max(c0,vec3f(0));let threshold=max(p(3),0);let knee=max(p(4),.0001);let brightness=max(c.r,max(c.g,c.b));let soft=clamp((brightness-threshold+knee)/(2*knee),0,1);let contribution=max(brightness-threshold,0)+soft*soft*knee;return c*(contribution/max(brightness,.0001));}
fn bloom_sample(uv:vec2f)->vec3f{let c=tex(uv).rgb;return select(c,bloom_prefilter(c),p(2)>.5);}
fn bloom_karis(a:vec3f,b:vec3f,c:vec3f,d:vec3f)->vec3f{let w=vec4f(1/(1+bloom_luma(a)),1/(1+bloom_luma(b)),1/(1+bloom_luma(c)),1/(1+bloom_luma(d)));return (a*w.x+b*w.y+c*w.z+d*w.w)/max(dot(w,vec4f(1)),.0001);}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let t=vec2f(p(0),p(1));let center=bloom_sample(i.uv);let inner=bloom_karis(bloom_sample(i.uv+t*vec2f(-1,-1)),bloom_sample(i.uv+t*vec2f(1,-1)),bloom_sample(i.uv+t*vec2f(-1,1)),bloom_sample(i.uv+t*vec2f(1,1)));let outer=bloom_karis(bloom_sample(i.uv+t*vec2f(-2,-2)),bloom_sample(i.uv+t*vec2f(2,-2)),bloom_sample(i.uv+t*vec2f(-2,2)),bloom_sample(i.uv+t*vec2f(2,2)));let axial=bloom_karis(bloom_sample(i.uv+t*vec2f(-2,0)),bloom_sample(i.uv+t*vec2f(2,0)),bloom_sample(i.uv+t*vec2f(0,-2)),bloom_sample(i.uv+t*vec2f(0,2)));return vec4f((center*.25+inner*.5+(outer+axial)*.125)*i.color.rgb,1);}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kBloomUpsample = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let t=vec2f(p(0),p(1))*max(p(2),.5);var c=tex(i.uv).rgb*4;c+=tex(i.uv+vec2f(-t.x,0)).rgb*2;c+=tex(i.uv+vec2f(t.x,0)).rgb*2;c+=tex(i.uv+vec2f(0,-t.y)).rgb*2;c+=tex(i.uv+vec2f(0,t.y)).rgb*2;c+=tex(i.uv+vec2f(-t.x,-t.y)).rgb;c+=tex(i.uv+vec2f(t.x,-t.y)).rgb;c+=tex(i.uv+vec2f(-t.x,t.y)).rgb;c+=tex(i.uv+vec2f(t.x,t.y)).rgb;return vec4f(c*(1.0/16.0)*i.color.rgb,1);}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kExposureMeter = R"wgsl(
fn exposure_luma(c:vec3f)->f32{return max(dot(max(c,vec3f(0)),vec3f(.2126,.7152,.0722)),.00001);}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{var v:array<f32,16>;var n=0;for(var y=0;y<4;y++){for(var x=0;x<4;x++){let uv=(vec2f(f32(x),f32(y))+.5)*.25;v[n]=log2(exposure_luma(tex(uv).rgb));n++;}}for(var a=1;a<16;a++){let value=v[a];var b=a-1;loop{if b<0||v[b]<=value{break;}v[b+1]=v[b];b--;}v[b+1]=value;}var average=0.0;for(var q=2;q<14;q++){average+=v[q];}average/=12;let ev=clamp(log2(.18)-average,p(0),p(1));return vec4f(exp2(ev),ev,0,1);}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kExposureAdapt = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let target=max(tex(vec2f(.5)).r,.00001);let previous=max(aux_load(vec2f(.5)).r,.00001);let speed=select(max(p(2),0),max(p(1),0),target>previous);let blend=select(1-exp(-speed*max(p(0),0)),1.0,p(3)>.5);let exposure=mix(previous,target,clamp(blend,0,1));return vec4f(exposure,log2(exposure),0,1);}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kExposureApply = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let hdr=tex(i.uv);let automatic=select(1.0,aux_load(vec2f(.5)).r,p(1)>.5);return vec4f(hdr.rgb*max(p(0),0)*automatic,hdr.a);}
)wgsl";

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kDepthPyramidDownsample = R"wgsl(
fn depth_range(uv:vec2f)->vec2f{let v=tex(uv);return select(v.rg,vec2f(v.r),p(2)>.5);}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let t=vec2f(p(0),p(1));let a=depth_range(i.uv+t*vec2f(-.5,-.5));let b=depth_range(i.uv+t*vec2f(.5,-.5));let c=depth_range(i.uv+t*vec2f(-.5,.5));let d=depth_range(i.uv+t*vec2f(.5,.5));return vec4f(min(min(a.x,b.x),min(c.x,d.x)),max(max(a.y,b.y),max(c.y,d.y)),0,1);}
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

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kTaa = R"wgsl(
fn taa_reprojection()->mat4x4f{return mat4x4f(
 vec4f(p(7),p(8),p(9),p(10)),vec4f(p(11),p(12),p(13),p(14)),
 vec4f(p(15),p(16),p(17),p(18)),vec4f(p(19),p(20),p(21),p(22)));}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{
 let t=vec2f(p(0),p(1));let currentSample=tex(i.uv);let current=currentSample.rgb;let linearDepth=select(currentSample.a,motion_load(i.uv).r,p(26)>.5);if p(4)<.5{return vec4f(current*i.color.rgb,linearDepth);}
 let currentY=hdr_ycocg(current);var lo=currentY;var hi=currentY;var sum=vec3f(0);var sumSq=vec3f(0);
 for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){let c=hdr_ycocg(tex(i.uv+vec2f(f32(x)*t.x,f32(y)*t.y)).rgb);lo=min(lo,c);hi=max(hi,c);sum+=c;sumSq+=c*c;}}
 let mean=sum/9;let sigma=sqrt(max(sumSq/9-mean*mean,vec3f(0)));lo=max(lo,mean-sigma*1.25);hi=min(hi,mean+sigma*1.25);
 var historyUV=i.uv+vec2f(p(5),p(6));
 if p(25)>.5&&linearDepth>.00001&&linearDepth<.9999{let n=max(p(23),.0001);let f=max(p(24),n+.001);let vd=mix(n,f,linearDepth);let z=(f-n*f/max(vd,n))/(f-n);let pc=taa_reprojection()*vec4f(i.uv*2-1,z,1);if abs(pc.w)>.000001{historyUV=pc.xy/pc.w*.5+.5;}}
 var objectMotion=vec2f(0);if p(26)>.5&&linearDepth>.00001&&linearDepth<.9999{var nearest=motion_load(i.uv);for(var my=-1;my<=1;my++){for(var mx=-1;mx<=1;mx++){let candidate=motion_load(i.uv+vec2f(f32(mx)*t.x,f32(my)*t.y));if candidate.r<nearest.r{nearest=candidate;}}}objectMotion=(nearest.gb-vec2f(.5))*2;historyUV+=objectMotion;}
 let inBounds=all(historyUV>=vec2f(0))&&all(historyUV<=vec2f(1));let hs=aux_cubic(clamp(historyUV,vec2f(0),vec2f(1)));let history=hs.rgb;let historyY=hdr_ycocg(history);let clippedY=mix(historyY,clamp(historyY,lo,hi),clamp(p(3),0,1));let clipped=hdr_rgb(clippedY);
 let encodedDelta=abs(currentY-clippedY);let delta=max(encodedDelta.x,max(encodedDelta.y,encodedDelta.z));let colorReject=smoothstep(.03,.15,delta);let dt=.006+linearDepth*.02;let depthReject=smoothstep(dt,dt*4,abs(linearDepth-hs.a));let velocityReactive=smoothstep(.002,.04,length(objectMotion));var surfaceReactive=0.0;if p(26)>.5&&linearDepth>.00001&&linearDepth<.9999{surfaceReactive=smoothstep(.01,.08,abs(linearDepth-motion_load(i.uv).r));}var reject=max(max(colorReject,depthReject),max(velocityReactive*.65,surfaceReactive));if !inBounds{reject=1;}
 let currentWeight=mix(clamp(p(2),0,1),1.0,reject);
 var resolved=mix(clipped,current,currentWeight);let crossAverage=(hdr_ycocg(tex(i.uv+vec2f(t.x,0)).rgb)+hdr_ycocg(tex(i.uv-vec2f(t.x,0)).rgb)+hdr_ycocg(tex(i.uv+vec2f(0,t.y)).rgb)+hdr_ycocg(tex(i.uv-vec2f(0,t.y)).rgb))*.25;let sharpenStrength=.12*(1-reject)*(1-velocityReactive);let sharpened=hdr_ycocg(resolved)+(currentY-crossAverage)*sharpenStrength;resolved=hdr_rgb(clamp(sharpened,lo,hi));
 return vec4f(resolved*i.color.rgb,linearDepth);
}
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
