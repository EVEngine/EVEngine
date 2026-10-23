#pragma once
namespace eve::stylize::shaders {
inline constexpr const char *kImpastoVert = R"wgsl(
struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f};
struct Params{data:array<vec4f,8>};
@group(0) @binding(0)var<uniform> frame:Frame;
@group(0) @binding(15)var<uniform> params:Params;
fn mp(i:u32)->f32{return params.data[i/4u][i%4u];}
struct PaintVarying{
 @builtin(position) position:vec4f,
 @location(0) normal:vec3f,@location(1) uv:vec2f,@location(2) tint:vec4f,
 @location(3) light:vec3f,@location(4) lightColor:vec3f,
 @location(5) world:vec3f,@location(6) camera:vec3f,@location(7) smoothNormal:vec3f,
};

struct PaintVertex{@location(0) position:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f,@location(5) color:vec4f};
fn normalWorld(n:vec3f)->vec3f{
 let a=frame.model[0].xyz;let b=frame.model[1].xyz;let c=frame.model[2].xyz;
 return normalize(mat3x3f(cross(b,c),cross(c,a),cross(a,b))*n/dot(a,cross(b,c)));
}
@vertex fn vs_main(i:PaintVertex)->PaintVarying{
 var smoothN=i.normal;
 if(mp(15)>1.5){smoothN=normalize(i.color.rgb*2.0-vec3f(1));}
 else if(mp(15)>0.5){smoothN=normalize(i.position+vec3f(0.00001));}
 var p=i.position;
 if(mp(12)>0.5){p+=smoothN*mp(13)+vec3f(0.57,0.83,0.21)*mp(14);}
 var o:PaintVarying;o.position=frame.mvp*vec4f(p,1);o.world=(frame.model*vec4f(p,1)).xyz;
 o.normal=normalWorld(i.normal);o.smoothNormal=normalWorld(smoothN);o.uv=i.uv;o.tint=frame.tint;
 o.light=normalize(frame.lightDir.xyz);o.lightColor=frame.lightColor.rgb;o.camera=frame.cameraPos.xyz;return o;
}
)wgsl";
inline constexpr const char *kImpastoMesh = R"wgsl(
struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f};
struct Params{data:array<vec4f,8>};
@group(0) @binding(0)var<uniform> frame:Frame;
@group(0) @binding(15)var<uniform> params:Params;
fn mp(i:u32)->f32{return params.data[i/4u][i%4u];}
struct PaintVarying{
 @builtin(position) position:vec4f,
 @location(0) normal:vec3f,@location(1) uv:vec2f,@location(2) tint:vec4f,
 @location(3) light:vec3f,@location(4) lightColor:vec3f,
 @location(5) world:vec3f,@location(6) camera:vec3f,@location(7) smoothNormal:vec3f,
};

@group(0) @binding(1)var brushMap:texture_2d<f32>;
@group(0) @binding(7)var brushSampler:sampler;
fn sdBox(p:vec3f,h:f32)->f32{let q=abs(p)-vec3f(h);return length(max(q,vec3f(0)))+min(max(q.x,max(q.y,q.z)),0.0);}
fn sceneDistance(p:vec3f)->f32{
 var d=1000.0;let owner=i32(mp(19)+0.5);
 if(owner!=0){d=p.y-mp(18);}
 if(owner!=1 && mp(23)>0.0){d=min(d,length(p-vec3f(mp(20),mp(21),mp(22)))-mp(23));}
 if(owner!=2 && mp(27)>0.0){d=min(d,length(p-vec3f(mp(24),mp(25),mp(26)))-mp(27));}
 if(owner!=3 && mp(31)>0.0){d=min(d,sdBox(p-vec3f(mp(28),mp(29),mp(30)),mp(31)));}return d;
}
fn sdfAO(p:vec3f,n:vec3f)->f32{
 var amount=0.0;var weight=1.0;let radius=max(mp(17),0.01);
 for(var j=1;j<=6;j++){let h=radius*f32(j)/6.0;amount+=max(h-sceneDistance(p+n*h),0.0)/radius*weight;weight*=0.66;}
 return clamp(amount*2.4,0.0,1.0);
}
fn sdfShadow(p:vec3f,l:vec3f)->f32{
 var t=0.035;var visibility=1.0;
 for(var j=0;j<28;j++){let d=sceneDistance(p+l*t);visibility=min(visibility,9.0*d/t);t+=clamp(d,0.035,0.4);if(t>9.0){break;}}
 return clamp(visibility,0.0,1.0);
}
@fragment fn fs_main(i:PaintVarying,@builtin(front_facing) front:bool)->@location(0) vec4f{
 let N=normalize(i.normal);let S=normalize(i.smoothNormal);let V=normalize(i.camera-i.world);let L=normalize(i.light);
 let dx=dpdx(i.world);let dy=dpdy(i.world);let ux=dpdx(i.uv);let uy=dpdy(i.uv);
 let det=ux.x*uy.y-ux.y*uy.x;let handed=select(1.0,-1.0,det<0.0);
 var T=(dx*uy.y-dy*ux.y)*handed;T-=N*dot(N,T);
 let fallback=cross(select(vec3f(1,0,0),vec3f(0,1,0),abs(N.y)<0.99),N);
 T=normalize(select(fallback,T,dot(T,T)>1e-10));let B=cross(N,T)*handed;
 var uv=i.uv*max(mp(0),0.1);var brush=textureSample(brushMap,brushSampler,uv);
 uv+=vec2f(dot(V,T),dot(V,B))*(brush.b-0.5)*mp(3);brush=textureSample(brushMap,brushSampler,uv);
 if(mp(11)<0.5){brush=vec4f(0.5);}
 let P=normalize(T*(brush.r*2.0-1.0)*mp(1)+B*(brush.g*2.0-1.0)*mp(1)+N*0.95);
 let viewUV=vec2f(atan2(V.z,V.x)*0.159155,asin(clamp(V.y,-1.0,1.0))*0.31831)*3.7;
 let edgeBrush=textureSample(brushMap,brushSampler,viewUV);
 let signedFacing=dot(S,V);let facing=abs(signedFacing);
 let curvature=length(dpdx(S))+length(dpdy(S));
 let facingSlope=length(vec2f(dpdx(signedFacing),dpdy(signedFacing)));
 var edgePixels=0.5*facing/max(facingSlope,0.000001);let owner=i32(mp(19)+0.5);
 if(owner==3 && mp(31)>0.0){
  let q=i.world-vec3f(mp(28),mp(29),mp(30));edgePixels=1000.0;
  for(var axis=0u;axis<3u;axis++){
   if(abs(N[axis])<0.5 && abs(V[axis])>0.02){
    let distanceToEdge=mp(31)+sign(V[axis])*q[axis];
    let worldPerPixel=length(vec2f(dx[axis],dy[axis]));
    edgePixels=min(edgePixels,max(distanceToEdge,0.0)/max(worldPerPixel,0.000001));
   }
  }
 }else if(owner==0 || (owner==4 && abs(dot(N,V))>0.25)){edgePixels=1000.0;}
 let layer=mp(12);
 // Evaluate derivative-dependent expressions before the masks diverge.
 let ndl=dot(P,L);let width=max(fwidth(ndl),0.012);
 let rb=textureSample(brushMap,brushSampler,uv.yx+vec2f(0.31,0.73));
 if(layer>0.5){
  if(front){discard;}
  let band=select(0.16,0.43,layer<1.5);let cut=select(0.57,0.30,layer<1.5);
  if(facing>band || edgeBrush.a<cut){discard;}
  let warm=smoothstep(-0.25,0.65,dot(S,L));
  var rimColor=mix(vec3f(0.42,0.15,0.64),vec3f(1.65,0.75,0.72),warm);
  rimColor=mix(rimColor,vec3f(1.9,1.65,1.05),step(0.68,edgeBrush.b)*warm);
  if(layer>1.5){rimColor=mix(vec3f(0.18,0.32,0.36),vec3f(1.6,1.7,1.45),step(0.5,layer-2.0));}
  return vec4f(rimColor,1);
 }
 let stripWidth=3.0*clamp(mp(10),0.0,1.0);
 let edgeMask=edgeBrush.a*0.65+edgeBrush.b*0.35;
 let erosionThreshold=(1.0-edgePixels/max(stripWidth,0.000001))*0.8;
 if(curvature>0.00015 && edgePixels<stripWidth && edgeMask<erosionThreshold){discard;}
 var shade=0.25*smoothstep(-0.22-width,-0.22+width,ndl)+0.75*smoothstep(0.32-width,0.32+width,ndl);
 var ao=0.0;if(mp(16)>0.0){ao=sdfAO(i.world+N*0.012,N)*mp(16);}
 var visibility=1.0;if(mp(5)>0.0){visibility=mix(1.0,sdfShadow(i.world+N*0.025,L),mp(5));}
 let paintedAO=smoothstep(0.12,0.30,ao-(brush.a-0.5)*0.48)*smoothstep(0.0,0.15,ao);
 let paintedShadow=smoothstep(0.24,0.44,visibility+(brush.a-0.5)*0.5);shade*=paintedShadow;
 let base=i.tint.rgb*mix(1.0-mp(2),1.0+mp(2),brush.b);
 var color=base*mix(vec3f(mp(6),mp(7),mp(8)),vec3f(1.12,1.02,0.86),shade);
 color=mix(color,color*vec3f(0.25,0.30,0.48),paintedAO*0.82);
 let RN=normalize(T*(rb.r*2.0-1.0)*mp(1)+B*(rb.g*2.0-1.0)*mp(1)+N*0.95);
 let reflected=dot(reflect(-V,RN),L);
 var reflection=mix(vec3f(0.035,0.11,0.10),vec3f(0.24,0.08,0.46),smoothstep(-0.45,0.05,reflected));
 reflection=mix(reflection,vec3f(0.91,0.29,0.27),smoothstep(0.16,0.36,reflected));
 reflection=mix(reflection,vec3f(1.35,1.10,0.69),smoothstep(0.50,0.68,reflected));
 color=mix(color,reflection,clamp(mp(4)*(0.35+0.45*rb.b),0.0,0.9)*(1.0-paintedAO*0.6));
 let rim=pow(1.0-facing,2.3)*smoothstep(-0.15,0.65,dot(S,L));
 color+=mix(vec3f(1.2,0.3,0.4),vec3f(1.5,1.3,0.8),brush.a)*rim*mp(9);
 return vec4f(color*clamp(i.lightColor,vec3f(0),vec3f(2)),i.tint.a);
}
)wgsl";
}  // namespace eve::stylize::shaders
