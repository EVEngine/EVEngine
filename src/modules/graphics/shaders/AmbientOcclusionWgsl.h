#pragma once

namespace eve::graphics::shaders {

// WebGPU uses a dynamic uniform buffer in place of Vulkan push constants.
// Eight vec4s preserve the exact packed float[32] layout used by Shader.
inline constexpr const char *kAoCommon = R"wgsl(
struct FSIn { @location(0) color: vec4f, @location(1) uv: vec2f };
struct Externals { data: array<vec4f, 8> };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(1) var normalTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@group(0) @binding(4) var<uniform> u: Externals;
fn p(i: u32) -> f32 { return u.data[i / 4u][i % 4u]; }
fn inv_vp() -> mat4x4f { return mat4x4f(u.data[0], u.data[1], u.data[2], u.data[3]); }
fn depth(uv: vec2f) -> f32 { return textureSampleLevel(mainTex, mainSamp, clamp(uv, vec2f(0), vec2f(1)), 0).r; }
fn normal_load(uv:vec2f)->vec3f{let d=textureDimensions(normalTex);let q=vec2i(clamp(uv,vec2f(0),vec2f(1))*vec2f(d));return textureLoad(normalTex,clamp(q,vec2i(0),vec2i(d)-1),0).xyz;}
fn world(m: mat4x4f, uv: vec2f, z: f32) -> vec3f {
  let h = m * vec4f(uv * 2 - 1, clamp(z, 0, 1), 1);
  return h.xyz / max(h.w, 0.000001);
}
fn hash12(q: vec2f) -> f32 {
  var q3 = fract(vec3f(q.x, q.y, q.x) * 0.1031);
  q3 += dot(q3, q3.yzx + 33.33);
  return fract((q3.x + q3.y) * q3.z);
}
)wgsl";

inline constexpr const char *kSsao = R"wgsl(
fn kernel(i: i32) -> vec3f {
  let f = f32(i); let z = fract(f * 0.6180339887); let a = f * 2.3999632297;
  let r = sqrt(max(1 - z*z, 0)) * mix(0.2, 1.0, (f + 1) / 24.0);
  return vec3f(cos(a)*r, sin(a)*r, z);
}
@fragment fn fs_main(i: FSIn) -> @location(0) vec4f {
  let m=inv_vp(); let nZ=max(p(16),.001); let fZ=max(p(17),nZ+.001);
  let rad=max(p(18),.0001); let bias=max(p(19),0); let count=i32(clamp(p(22),4,24));
  let tw=max(p(23),.00001); let th=max(p(24),.00001); let z=depth(i.uv);
  if z>=.999 || z<=.00001 { return vec4f(1,1,1,z); }
  let pos=world(m,i.uv,z); let pr=world(m,i.uv+vec2f(tw,0),depth(i.uv+vec2f(tw,0)));
  let pu=world(m,i.uv+vec2f(0,th),depth(i.uv+vec2f(0,th)));
  var nor=normalize(cross(pr-pos,pu-pos)); let eh=m*vec4f(0,0,0,1); let cam=eh.xyz/max(eh.w,.000001);
  if dot(nor,cam-pos)<0 { nor=-nor; }
  let a=hash12(i.uv*vec2f(1024,768))*6.2831853; let up=select(vec3f(0,1,0),vec3f(1,0,0),abs(nor.y)>=.999);
  var tan=normalize(up-nor*dot(up,nor)); let bit=cross(nor,tan); tan=tan*cos(a)+bit*sin(a);
  let tbn=mat3x3f(tan,cross(nor,tan),nor); let rz=mix(nZ,fZ,z);
  let ur=clamp((rad/max(rz,.001))*.25,tw*2,.2); var occ=0.0;
  for(var s=0;s<24;s++){ if s>=count {break;} let k=kernel(s); let o=(tbn*k).xy;
    let uv=clamp(i.uv+normalize(o+vec2f(.00001))*ur*length(k.xy),vec2f(0),vec2f(1));
    let sz=depth(uv); let sp=world(m,uv,sz); let d=sp-pos; let dist=length(d);
    if dist>.00001 { let nd=dot(nor,d/dist); let range=1-smoothstep(rad*.5,rad,dist);
      if nd>bias && mix(nZ,fZ,sz)<rz-bias { occ+=range; } }
  }
  var ao=1-occ/f32(max(count,1))*clamp(p(20),0,2); ao=clamp(pow(max(ao,0),max(p(21),.01)),0,1);
  return vec4f(ao,ao,ao,z)*i.color;
}
)wgsl";

inline constexpr const char *kHbao = R"wgsl(
@fragment fn fs_main(i: FSIn) -> @location(0) vec4f {
 let m=inv_vp(); let nz=max(p(16),.001); let fz=max(p(17),nz+.001); let r=max(p(18),.0001);
 let bias=max(p(19),0); let dirs=i32(clamp(p(22),2,8)); let steps=i32(clamp(p(23),2,8));
 let tw=max(p(24),.00001); let th=max(p(25),.00001); let z=depth(i.uv); if z>=.999||z<=.00001{return vec4f(1,1,1,z);}
 let pos=world(m,i.uv,z); var n=normalize(cross(world(m,i.uv+vec2f(tw,0),depth(i.uv+vec2f(tw,0)))-pos,world(m,i.uv+vec2f(0,th),depth(i.uv+vec2f(0,th)))-pos));
 let eh=m*vec4f(0,0,0,1); if dot(n,eh.xyz/max(eh.w,.000001)-pos)<0{n=-n;}
 let sr=clamp(r/max(mix(nz,fz,z),.001)*.35,tw*4,.25); let rnd=hash12(i.uv*vec2f(900,700)); var sum=0.0;
 for(var d=0;d<8;d++){if d>=dirs{break;} let a=(f32(d)+rnd)/f32(dirs)*6.2831853; let dir=vec2f(cos(a),sin(a)); var h=bias;
  for(var s=1;s<=8;s++){if s>steps{break;} let uv=i.uv+dir*sr*f32(s)/f32(steps); let delta=world(m,uv,depth(uv))-pos; let dist=length(delta);
   if dist>.00001&&dist<r {h=max(h,dot(normalize(delta),n)*(1-clamp(dist/r,0,1)));}}
  sum+=1-clamp(h,0,1);
 }
 var ao=sum/f32(max(dirs,1)); ao=clamp(pow(max(ao,0),max(p(21),.01)),0,1); ao=mix(1,ao,clamp(p(20),0,2)); return vec4f(ao,ao,ao,z)*i.color;
}
)wgsl";

inline constexpr const char *kGtao = R"wgsl(
fn integrate(a:f32,b:f32)->f32{return clamp(.25*(-cos(2*b)+cos(2*a)+2*(b-a)),0,1);}
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{
 let m=inv_vp();let nz=max(p(16),.001);let fz=max(p(17),nz+.001);let r=max(p(18),.0001);let bias=max(p(19),0);
 let dirs=i32(clamp(p(22),2,8));let steps=i32(clamp(p(23),2,8));let tw=max(p(24),.00001);let th=max(p(25),.00001);let thick=max(p(26),r*.5);
 let z=depth(i.uv);if z>=.999||z<=.00001{return vec4f(1,1,1,z);}let pos=world(m,i.uv,z);
 var n=normalize(cross(world(m,i.uv+vec2f(tw,0),depth(i.uv+vec2f(tw,0)))-pos,world(m,i.uv+vec2f(0,th),depth(i.uv+vec2f(0,th)))-pos));
 let eh=m*vec4f(0,0,0,1);if dot(n,eh.xyz/max(eh.w,.000001)-pos)<0{n=-n;}let sr=clamp(r/max(mix(nz,fz,z),.001)*.4,tw*4,.25);
 let rnd=hash12(i.uv*vec2f(640,480));let hp=-1.5707963+bias;var vis=0.0;
 for(var d=0;d<8;d++){if d>=dirs{break;}let a=(f32(d)+rnd)/f32(dirs)*3.14159265;let dir=vec2f(cos(a),sin(a));var h1=hp;var h2=hp;
  for(var s=1;s<=8;s++){if s>steps{break;}let q=sr*f32(s)/f32(steps);
   let dp=world(m,i.uv+dir*q,depth(i.uv+dir*q))-pos;let dn=world(m,i.uv-dir*q,depth(i.uv-dir*q))-pos;let lp=length(dp);let ln=length(dn);
   if lp>.0001&&lp<r{h1=max(h1,mix(-1.5707963,asin(clamp(dot(normalize(dp),n),-1,1)),1-lp/r));}
   if ln>.0001&&ln<r{h2=max(h2,mix(-1.5707963,asin(clamp(dot(normalize(dn),n),-1,1)),1-ln/r));}}
  let c=.5*(integrate(-1.5707963,clamp(h1,-1.5707963,1.5707963))+integrate(-1.5707963,clamp(h2,-1.5707963,1.5707963)));
  let thin=smoothstep(0,1,(h1+h2+3.14159265)/max(thick/max(r,.001),.001));vis+=mix(c,max(c,.85),.2*(1-thin));}
 vis/=f32(max(dirs,1));var ao=clamp(pow(max(vis,0),max(p(21),.01)),0,1);ao=mix(1,ao,clamp(p(20),0,2));return vec4f(ao,ao,ao,z)*i.color;
}
)wgsl";

inline constexpr const char *kBlur = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{let tw=max(p(0),.00001);let th=max(p(1),.00001);let ds=max(p(2),.0001);let k=clamp(p(3),1,4);
 let c=textureSampleLevel(mainTex,mainSamp,i.uv,0);var sum=c.r;var ws=1.0;
 for(var y=-2;y<=2;y++){for(var x=-2;x<=2;x++){if x==0&&y==0{continue;}let d=length(vec2f(f32(x),f32(y)));if d>k+.1{continue;}
  let s=textureSampleLevel(mainTex,mainSamp,clamp(i.uv+vec2f(f32(x)*tw,f32(y)*th),vec2f(0),vec2f(1)),0);let dz=abs(s.a-c.a);
  let w=exp(-d*d/(2*k*k))*exp(-(dz*dz)/(2*ds*ds));sum+=s.r*w;ws+=w;}}
 let ao=sum/max(ws,.00001);return vec4f(ao,ao,ao,c.a)*i.color;}
)wgsl";

inline constexpr const char *kOverlay = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{var ao=depth(i.uv);ao=clamp(pow(max(ao,0),max(p(1),.01)),0,1);let d=clamp((1-ao)*max(p(0),0),0,1);return vec4f(0,0,0,d)*i.color;}
)wgsl";

// The direct GBuffer path intentionally shares the SSAO sampling model. Its
// output is an alpha-only darkening overlay, matching the Vulkan pass contract.
inline constexpr const char *kFromDepth = R"wgsl(
@fragment fn fs_main(i:FSIn)->@location(0) vec4f{
 let m=inv_vp();let nz=max(p(16),.001);let fz=max(p(17),nz+.001);let r=max(p(18),.0001);let bias=max(p(19),0);let count=i32(clamp(p(22),4,24));
 let tw=max(p(23),.00001);let th=max(p(24),.00001);let z=depth(i.uv);if z>=.9999{return vec4f(0);}let pos=world(m,i.uv,z);
 var n:vec3f;if p(25)>.5{let q=normal_load(i.uv)*2-1;n=normalize(select(vec3f(0,1,0),q,length(q)>.001));}
 else{n=normalize(cross(world(m,i.uv+vec2f(tw,0),depth(i.uv+vec2f(tw,0)))-pos,world(m,i.uv+vec2f(0,th),depth(i.uv+vec2f(0,th)))-pos));}
 let eh=m*vec4f(0,0,0,1);if dot(n,eh.xyz/max(eh.w,.000001)-pos)<0{n=-n;}var occ=0.0;
 for(var s=1;s<=24;s++){if s>count{break;}let a=f32(s)*2.39996323;let q=f32(s)/f32(count);let uv=clamp(i.uv+vec2f(cos(a),sin(a))*q*r/max(mix(nz,fz,z),.001)*.25,vec2f(0),vec2f(1));
  let sz=depth(uv);let d=world(m,uv,sz)-pos;let l=length(d);if l>.00001&&l<r&&dot(n,d/l)>bias&&sz<z-bias{occ+=1-l/r;}}
 var ao=1-occ/f32(max(count,1));ao=clamp(pow(max(ao,0),max(p(21),.01)),0,1);let dark=clamp((1-ao)*max(p(20),0),0,1);return vec4f(0,0,0,dark)*i.color;
}
)wgsl";

}  // namespace eve::graphics::shaders
