#version 450
// Packed brush: RG tangent normal, B stroke height/tone, A independent erosion mask.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vLightDir;
layout(location=4) in vec3 vLightColor;
layout(location=5) in vec3 vWorldPos;
layout(location=6) in vec3 vCameraPos;
layout(location=7) in vec3 vSmoothNormal;
layout(set=0,binding=1) uniform sampler2D brushMap;
layout(push_constant) uniform Params {float data[32];} u;
layout(location=0) out vec4 outColor;
float sdBox(vec3 p,float halfSize){vec3 q=abs(p)-vec3(halfSize);return length(max(q,0.0))+min(max(q.x,max(q.y,q.z)),0.0);}
float sceneDistance(vec3 p){
 float d=1000.0;int owner=int(u.data[19]+0.5);
 if(owner!=0)d=p.y-u.data[18];
 if(owner!=1 && u.data[23]>0.0)d=min(d,length(p-vec3(u.data[20],u.data[21],u.data[22]))-u.data[23]);
 if(owner!=2 && u.data[27]>0.0)d=min(d,length(p-vec3(u.data[24],u.data[25],u.data[26]))-u.data[27]);
 if(owner!=3 && u.data[31]>0.0)d=min(d,sdBox(p-vec3(u.data[28],u.data[29],u.data[30]),u.data[31]));
 return d;
}
float sdfAO(vec3 p,vec3 n){
 float amount=0.0;float weight=1.0;float radius=max(u.data[17],0.01);
 for(int i=1;i<=6;i++){float h=radius*float(i)/6.0;amount+=max(h-sceneDistance(p+n*h),0.0)/radius*weight;weight*=0.66;}
 return clamp(amount*2.4,0.0,1.0);
}
float sdfShadow(vec3 p,vec3 l){
 float t=0.035;float visibility=1.0;
 for(int i=0;i<28;i++){float d=sceneDistance(p+l*t);visibility=min(visibility,9.0*d/t);t+=clamp(d,0.035,0.4);if(t>9.0)break;}
 return clamp(visibility,0.0,1.0);
}
void main(){
 vec3 N=normalize(vNormal),S=normalize(vSmoothNormal),V=normalize(vCameraPos-vWorldPos),L=normalize(vLightDir);
 vec3 dx=dFdx(vWorldPos),dy=dFdy(vWorldPos);vec2 ux=dFdx(vUV),uy=dFdy(vUV);
 float det=ux.x*uy.y-ux.y*uy.x;float handed=det<0.0?-1.0:1.0;
 vec3 T=(dx*uy.y-dy*ux.y)*handed;T-=N*dot(N,T);
 vec3 fallback=cross(abs(N.y)<0.99?vec3(0,1,0):vec3(1,0,0),N);
 T=normalize(dot(T,T)>1e-10?T:fallback);vec3 B=cross(N,T)*handed;
 float scale=max(u.data[0],0.1);vec2 uv=vUV*scale;
 vec4 brush=texture(brushMap,uv);uv+=vec2(dot(V,T),dot(V,B))*(brush.b-0.5)*u.data[3];
 brush=texture(brushMap,uv);
 // Albedo slot contains a data texture only when explicitly enabled.
 if(u.data[11]<0.5)brush=vec4(0.5,0.5,0.5,0.5);
 vec3 P=normalize(T*(brush.r*2.0-1.0)*u.data[1]+B*(brush.g*2.0-1.0)*u.data[1]+N*0.95);
 // Camera-vector mapping follows the reference rather than a screen-space overlay.
 vec2 viewUV=vec2(atan(V.z,V.x)*0.159155,asin(clamp(V.y,-1.0,1.0))*0.31831)*3.7;
 vec4 edgeBrush=texture(brushMap,viewUV);
 float signedFacing=dot(S,V);float facing=abs(signedFacing);
 float curvature=length(dFdx(S))+length(dFdy(S));
 // Estimate screen distance to the silhouette, never a broad Fresnel region.
 float facingSlope=length(vec2(dFdx(signedFacing),dFdy(signedFacing)));
 float edgePixels=0.5*facing/max(facingSlope,0.000001);
 int owner=int(u.data[19]+0.5);
 if(owner==3 && u.data[31]>0.0){
  // Exact visible contour of the supplied axis-aligned box. Adjacent visible
  // faces share an internal crease: do not cut that crease or the face interior.
  vec3 q=vWorldPos-vec3(u.data[28],u.data[29],u.data[30]);
  edgePixels=1000.0;
  for(int axis=0;axis<3;axis++){
   if(abs(N[axis])<0.5 && abs(V[axis])>0.02){
    float distanceToEdge=u.data[31]+sign(V[axis])*q[axis];
    float worldPerPixel=length(vec2(dx[axis],dy[axis]));
    edgePixels=min(edgePixels,max(distanceToEdge,0.0)/max(worldPerPixel,0.000001));
   }
  }
 }else if(owner==0 || (owner==4 && abs(dot(N,V))>0.25)){
  edgePixels=1000.0;
 }
 float ndl=dot(P,L);float width=max(fwidth(ndl),0.012);
 vec4 rb=texture(brushMap,uv.yx+vec2(0.31,0.73));
 float layer=u.data[12];
 if(layer>0.5){
  // Two-sided masked inverted hull: suppress front faces; the solid masks the interior.
  if(gl_FrontFacing)discard;
  float band=layer<1.5?0.43:0.16;
  float cut=layer<1.5?0.30:0.57;
  if(facing>band || edgeBrush.a<cut)discard;
  float warm=smoothstep(-0.25,0.65,dot(S,L));
  vec3 rimColor=mix(vec3(0.42,0.15,0.64),vec3(1.65,0.75,0.72),warm);
  rimColor=mix(rimColor,vec3(1.9,1.65,1.05),step(0.68,edgeBrush.b)*warm);
  if(layer>1.5)rimColor=mix(vec3(0.18,0.32,0.36),vec3(1.6,1.7,1.45),step(0.5,layer-2.0));
  outColor=vec4(rimColor,1.0);return;
 }
 // rimErosion controls a bounded 0..3 pixel strip, independent of mesh size.
 float stripWidth=3.0*clamp(u.data[10],0.0,1.0);
 float edgeMask=edgeBrush.a*0.65+edgeBrush.b*0.35;
 float erosionThreshold=(1.0-edgePixels/max(stripWidth,0.000001))*0.8;
 if(curvature>0.00015 && edgePixels<stripWidth && edgeMask<erosionThreshold)discard;
 float shade=0.25*smoothstep(-0.22-width,-0.22+width,ndl)+0.75*smoothstep(0.32-width,0.32+width,ndl);
 float ao=0.0;
 if(u.data[16]>0.0)ao=sdfAO(vWorldPos+N*0.012,N)*u.data[16];
 float visibility=1.0;
 if(u.data[5]>0.0)visibility=mix(1.0,sdfShadow(vWorldPos+N*0.025,L),u.data[5]);
 // Alpha breaks both AO and cast-shadow gradients into coherent brush islands.
 float paintedAO=smoothstep(0.12,0.30,ao-(brush.a-0.5)*0.48)*smoothstep(0.0,0.15,ao);
 float paintedShadow=smoothstep(0.24,0.44,visibility+(brush.a-0.5)*0.5);
 shade*=paintedShadow;
 vec3 base=vTint.rgb*mix(1.0-u.data[2],1.0+u.data[2],brush.b);
 vec3 dark=vec3(u.data[6],u.data[7],u.data[8]);
 vec3 color=base*mix(dark,vec3(1.12,1.02,0.86),shade);
 color=mix(color,color*vec3(0.25,0.30,0.48),paintedAO*0.82);
 vec3 RN=normalize(T*(rb.r*2.0-1.0)*u.data[1]+B*(rb.g*2.0-1.0)*u.data[1]+N*0.95);
 float reflected=dot(reflect(-V,RN),L);
 // Color ramps over the reflection vector, not a glittering Phong point highlight.
 vec3 reflection=mix(vec3(0.035,0.11,0.10),vec3(0.24,0.08,0.46),smoothstep(-0.45,0.05,reflected));
 reflection=mix(reflection,vec3(0.91,0.29,0.27),smoothstep(0.16,0.36,reflected));
 reflection=mix(reflection,vec3(1.35,1.10,0.69),smoothstep(0.50,0.68,reflected));
 color=mix(color,reflection,clamp(u.data[4]*(0.35+0.45*rb.b),0.0,0.9)*(1.0-paintedAO*0.6));
 float rim=pow(1.0-facing,2.3)*smoothstep(-0.15,0.65,dot(S,L));
 color+=mix(vec3(1.2,0.3,0.4),vec3(1.5,1.3,0.8),brush.a)*rim*u.data[9];
 outColor=vec4(color*clamp(vLightColor,vec3(0.0),vec3(2.0)),vTint.a);
}
