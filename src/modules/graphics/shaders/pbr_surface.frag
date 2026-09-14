#version 450
struct UvInfo { float rotation; uint offset; float present; float srgb; };
struct Light { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Params {
 mat4 mvp; mat4 model; mat4 view;
 vec4 camera; vec4 tint; vec4 ambient; vec4 material;
 vec4 emissive; vec4 specular; vec4 coat; vec4 misc;
 Light lights[8];
 vec4 uvTransform[11]; // offset xy, scale zw
 UvInfo uvInfo[11]; // rotation, stream offset (-1 uses vertex UV0), present, reserved
} u;

layout(set=0,binding=1) uniform sampler2D maps[11];
layout(set=0,binding=4) uniform samplerCube environmentMap;
layout(set=0,binding=5,std140) uniform Shadows {
 mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadow;
layout(set=0,binding=6) uniform sampler2DArrayShadow shadowMap;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 texcoord0;
layout(location=3) in vec2 texcoord1;
layout(location=4) in vec2 texcoord2;
layout(location=5) in vec2 texcoord3;
layout(location=6) in vec2 texcoord4;
layout(location=7) in vec2 texcoord5;
layout(location=8) in vec2 texcoord6;
layout(location=9) in vec2 texcoord7;
layout(location=10) in vec2 texcoord8;
layout(location=11) in vec2 texcoord9;
layout(location=12) in vec2 texcoord10;
layout(location=0) out vec4 outColor;
const float PI=3.14159265359;
vec3 linearColor(vec3 c) { return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c)); }
vec3 safeNormal(vec3 n, vec3 fallback) { return dot(n,n)>1e-12 ? normalize(n):fallback; }
mat3 basis(vec3 n,vec2 uv) {
 vec3 dx=dFdx(worldPosition),dy=dFdy(worldPosition);
 vec2 tx=dFdx(uv),ty=dFdy(uv);
 float det=tx.x*ty.y-tx.y*ty.x;
 vec3 fallback=safeNormal(cross(abs(n.z)<.99?vec3(0,0,1):vec3(0,1,0),n),vec3(1,0,0));
 vec3 t=abs(det)>1e-10 ? (dx*ty.y-dy*tx.y)/det : fallback;
 t=safeNormal(t-n*dot(n,t),fallback);
 return mat3(t,cross(n,t)*(det<0?-1:1),n);
}
vec3 fresnel(vec3 f0,vec3 f90,float x) { return f0+(f90-f0)*pow(1-clamp(x,0,1),5); }
vec3 lobe(vec3 n,vec3 t,vec3 b,vec3 v,vec3 l,float at,float ab,vec3 f0,vec3 f90) {
 vec3 h=safeNormal(v+l,n);
 float nv=max(dot(n,v),.0001),nl=max(dot(n,l),0),nh=max(dot(n,h),0);
 float q=pow(dot(t,h)/at,2)+pow(dot(b,h)/ab,2)+nh*nh;
 float d=1/max(PI*at*ab*q*q,1e-10);
 float gv=nl*length(vec3(at*dot(t,v),ab*dot(b,v),nv));
 float gl=nv*length(vec3(at*dot(t,l),ab*dot(b,l),nl));
 return fresnel(f0,f90,dot(v,h))*d*(.5/max(gv+gl,1e-7))*nl;
}
float visibility() {
 if(shadow.bias.y<.5 || shadow.bias.z<.5) return 1;
 float depth=-(u.view*vec4(worldPosition,1)).z;
 int c=depth<shadow.splits.x?0:(depth<shadow.splits.y?1:2);
 if(depth>shadow.splits.z) return 1;
 vec4 p=shadow.lightVP[c]*vec4(worldPosition,1); vec3 q=p.xyz/p.w;
 q.xy=q.xy*.5+.5;
 if(any(lessThan(q,vec3(0)))||any(greaterThan(q,vec3(1)))) return 1;
 float sum=0; float stepUV=1.0/float(textureSize(shadowMap,0).x);
 for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++)
  sum+=texture(shadowMap,vec4(q.xy+vec2(x,y)*stepUV,c,q.z-shadow.cascadeBias[c]));
 return mix(1,sum/9,shadow.splits.w);
}
void main() {
 vec4 base=u.tint;
 if(u.uvInfo[0].present>0) { vec4 s=texture(maps[0],texcoord0); base*=vec4(u.uvInfo[0].srgb>0?linearColor(s.rgb):s.rgb,s.a); }
 if(u.material.z==1 && base.a<u.material.w) discard;
 if(u.misc.z>0) { outColor=vec4(u.material.z==3?base.rgb*base.a:base.rgb,u.material.z>=2?base.a:1); return; }
 float metallic=u.material.x,roughness=u.material.y;
 if(u.uvInfo[1].present>0) { vec4 s=texture(maps[1],texcoord1); metallic*=s.b; roughness*=s.g; }
 roughness=clamp(roughness,.045,1);
 vec3 ng=safeNormal(worldNormal,vec3(0,0,1))*(gl_FrontFacing?1:-1);
 vec3 n=ng;
 if(u.uvInfo[2].present>0) { vec3 s=texture(maps[2],texcoord2).xyz*2-1;s.xy*=u.misc.x;n=safeNormal(basis(ng,texcoord2)*s,ng); }
 float ao=1;
 if(u.uvInfo[3].present>0) ao=mix(1,texture(maps[3],texcoord3).r,u.misc.y);
 vec3 emission=u.emissive.rgb*u.emissive.w;
 if(u.uvInfo[4].present>0) {vec3 c=texture(maps[4],texcoord4).rgb;emission*=u.uvInfo[4].srgb>0?linearColor(c):c;}
 float specWeight=u.specular.w;
 if(u.uvInfo[5].present>0) specWeight*=texture(maps[5],texcoord5).a;
 vec3 specColor=u.specular.rgb;
 if(u.uvInfo[6].present>0) {vec3 c=texture(maps[6],texcoord6).rgb;specColor*=u.uvInfo[6].srgb>0?linearColor(c):c;}
 float ior=u.coat.w;
 float dielectric=ior==0?1:pow((ior-1)/(ior+1),2);
 vec3 f0=mix(min(vec3(dielectric)*specColor,vec3(1))*specWeight,base.rgb,metallic);
 vec3 f90=vec3(mix(specWeight,1,metallic));
 vec3 v=safeNormal(u.camera.xyz-worldPosition,n);
 mat3 bn=basis(n,texcoord2);
 float strength=u.ambient.w,angle=u.camera.w;
 vec2 direction=vec2(cos(angle),sin(angle));
 if(u.uvInfo[7].present>0) { vec3 a=texture(maps[7],texcoord7).rgb;vec2 d=a.rg*2-1;
  d=dot(d,d)>1e-10?normalize(d):vec2(1,0);direction=mat2(direction.x,direction.y,-direction.y,direction.x)*d;strength*=a.b; }
 vec3 t=safeNormal(bn*vec3(direction,0),bn[0]),b=safeNormal(cross(n,t),bn[1]);
 float ab=roughness*roughness,at=mix(ab,1,strength*strength);
 float coat=u.coat.x,cr=u.coat.y;
 if(u.uvInfo[8].present>0) coat*=texture(maps[8],texcoord8).r;
 if(u.uvInfo[9].present>0) cr*=texture(maps[9],texcoord9).g;
 vec3 cn=ng;
 if(u.uvInfo[10].present>0) {vec3 s=texture(maps[10],texcoord10).xyz*2-1;s.xy*=u.coat.z;cn=safeNormal(basis(ng,texcoord10)*s,ng);}
 mat3 cb=basis(cn,texcoord10);float ca=max(cr*cr,.002025);
 vec3 color=vec3(0);float vis=visibility();
 for(int i=0;i<8;i++) {
  vec3 l; float attenuation=1;
  if(u.lights[i].posRadius.w<=0) l=safeNormal(u.lights[i].posRadius.xyz,n);
  else {vec3 d=u.lights[i].posRadius.xyz-worldPosition;float distance=length(d);l=safeNormal(d,n);attenuation=pow(max(1-distance/u.lights[i].posRadius.w,0),2);}
  vec3 h=safeNormal(v+l,n);
  vec3 f=fresnel(f0,f90,dot(v,h));
  vec3 diffuse=(1-f)*(1-metallic)*base.rgb/PI*max(dot(n,l),0);
  vec3 spec=lobe(n,t,b,v,l,at,ab,f0,f90);
  float cf=.04+.96*pow(1-max(dot(cn,v),0),5);
  vec3 total=(diffuse+spec)*(1-coat*cf)+coat*lobe(cn,cb[0],cb[1],v,l,ca,ca,vec3(.04),vec3(1));
  color+=total*u.lights[i].color.rgb*attenuation*(u.lights[i].posRadius.w<=0?vis:1);
 }
 color+=u.ambient.rgb*(base.rgb*(1-metallic)*(1-f0)+f0)*ao;
 if(u.lights[0].color.w>0) {
  vec3 bent=safeNormal(cross(cross(b,v),b),n);
  bent=safeNormal(mix(bent,n,pow(1-strength*(1-roughness),4)),n);
  vec3 reflected=reflect(-v,bent);
  float levels=float(textureQueryLevels(environmentMap)-1);
  color+=textureLod(environmentMap,reflected,roughness*levels).rgb*fresnel(f0,f90,max(dot(n,v),0))*u.lights[0].color.w*ao;
  color+=coat*textureLod(environmentMap,reflect(-v,cn),cr*levels).rgb*.04*u.lights[0].color.w;
 }
 color+=emission;
 outColor=vec4(u.material.z==3?color*base.a:color,u.material.z>=2?base.a:1);
}
