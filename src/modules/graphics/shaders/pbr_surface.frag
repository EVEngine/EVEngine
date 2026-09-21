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
 vec4 colorMaskSecondary; vec4 colorMaskParams;
 uvec4 tangentInfo;
 vec4 translucencyColor; vec4 translucencyParams; vec4 translucencyLighting;
 vec4 translucencyMask;
 vec4 motionHighlightColor;
 vec4 vegetationOverlay; vec4 vegetationWetness;
 vec4 vegetationStageParams;
 vec4 vegetationFieldColor; vec4 vegetationColorParams; vec4 vegetationOcclusionParams;
 vec4 vegetationOcclusionColor;
 vec4 vegetationGlobalMasks;
 uvec4 vegetationInfo;
 vec4 detailUv; vec4 detailColor; vec4 detailColorTwo; vec4 detailValues;
 vec4 detailMaterial; vec4 detailMasks; uvec4 detailInfo; uvec4 detailInfo2;
 vec4 extrasCoords; vec4 extrasFallback; vec4 extrasUsage0; vec4 extrasUsage1; vec4 extrasUsage2;
 uvec4 extrasInfo;
 vec4 vegetationAlphaFade; vec4 vegetationAlphaCamera; vec4 vegetationEmission;
 vec4 vegetationGradientOne; vec4 vegetationGradientTwo;
 vec4 colorsCoords; vec4 colorsFallback; vec4 colorsUsage0; vec4 colorsUsage1; vec4 colorsUsage2;
 uvec4 colorsInfo;
} u;

layout(set=0,binding=1) uniform sampler2D maps[11];
layout(set=0,binding=4) uniform samplerCube environmentMap;
layout(set=0,binding=5,std140) uniform Shadows {
 mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadow;
layout(set=0,binding=6) uniform sampler2DArrayShadow shadowMap;
layout(set=0,binding=7) uniform sampler2D detailMaps[3];
layout(set=0,binding=8) uniform sampler2DArray extrasMap;
layout(set=0,binding=9) uniform sampler2DArray colorsMap;
layout(set=0,binding=13) uniform sampler3D vegetationFadeNoise;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec4 texcoord01;
layout(location=3) in vec4 texcoord23;
layout(location=4) in vec4 texcoord45;
layout(location=5) in vec4 texcoord67;
layout(location=6) in vec4 texcoord89;
layout(location=7) in vec2 texcoord10;
layout(location=8) in vec2 normalBasisUv;
layout(location=9) in vec3 worldTangent;
layout(location=10) in vec3 worldBitangent;
layout(location=11) in float motionHighlight;
layout(location=12) in vec2 vegetationFactors;
layout(location=13) in vec3 vegetationDetail;
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
 vec4 globalExtras=vec4(1);
 if(u.extrasInfo.z!=0u) {
  vec3 samplePosition=u.extrasInfo.y!=0u?u.model[3].xyz:worldPosition;
  vec2 extrasUv=u.extrasCoords.zw+u.extrasCoords.xy*samplePosition.xz;
  float usage=u.extrasInfo.x<4u?u.extrasUsage0[u.extrasInfo.x]:(u.extrasInfo.x<8u?u.extrasUsage1[u.extrasInfo.x-4u]:u.extrasUsage2.x);
  globalExtras=usage>=.5?textureLod(extrasMap,vec3(extrasUv,float(u.extrasInfo.x)),0):u.extrasFallback;
 }
 vec4 base=u.tint;
 if(u.colorMaskParams.x>0) {
  float mask=texture(maps[1],texcoord01.zw).a;
  float remapped=clamp((clamp(mask,.0001,.9999)-u.colorMaskParams.y)/(u.colorMaskParams.z-u.colorMaskParams.y+.0001),0,1);
  base.rgb=mix(u.colorMaskSecondary.rgb,base.rgb,remapped);
 }
 if(u.uvInfo[0].present>0) { vec4 s=texture(maps[0],texcoord01.xy); base*=vec4(mix(vec3(1),u.uvInfo[0].srgb>0?linearColor(s.rgb):s.rgb,u.colorMaskParams.w),s.a); }
 float mainMask=u.uvInfo[1].present>0?texture(maps[1],texcoord01.zw).a:1;
 float blendMaskRemapped=clamp((clamp(mainMask,.0001,.9999)-u.colorMaskParams.y)/(u.colorMaskParams.z-u.colorMaskParams.y+.0001),0,1);
 vec3 ng=safeNormal(worldNormal,vec3(0,0,1));
 vec3 n=ng;
 vec3 mappedNormal=vec3(0,0,1);
 mat3 normalFrame=basis(ng,texcoord23.xy);
 if(u.uvInfo[2].present>0) {
  vec4 packed=texture(maps[2],texcoord23.xy);
  mappedNormal=packed.xyz*2-1;
  int mode=int(u.colorMaskSecondary.w);
  if(mode>0) {
   float x=mode==1?packed.r:(mode==2?packed.r*packed.a:packed.a);
   mappedNormal=vec3(vec2(x,packed.g)*2-1,1);
  }
  mappedNormal.xy*=u.misc.x;
  normalFrame=basis(ng,mode>0?normalBasisUv:texcoord23.xy);
  if(mode>0 && u.tangentInfo.y!=0u) {
   vec3 t=safeNormal(worldTangent-ng*dot(ng,worldTangent),normalFrame[0]);
   vec3 b=cross(ng,t);
   if(dot(b,worldBitangent)<0) b=-b;
   normalFrame=mat3(t,b,ng);
  }
  n=safeNormal(normalFrame*mappedNormal,ng);
 }
 float detailBlend=0;
 vec4 secondAlbedo=vec4(1); vec4 secondMask=vec4(1); vec3 secondNormal=vec3(0,0,1);
 if(u.detailValues.x>0) {
  vec2 detailSource=u.detailInfo.x==1u?vegetationDetail.yz:(u.detailInfo.x==2u?worldPosition.xz:texcoord01.xy);
  vec2 detailScale=u.detailInfo2.z!=0u?vec2(1)/u.detailUv.xy:u.detailUv.xy;
  vec2 detailUv=detailSource*detailScale+u.detailUv.zw;
  secondAlbedo=texture(detailMaps[0],detailUv);
  secondAlbedo.rgb=mix(vec3(1),linearColor(secondAlbedo.rgb),u.detailValues.z);
  if(u.detailInfo2.w>1u) secondMask=texture(detailMaps[2],detailUv);
  float secondMaskRemap=clamp((clamp(secondMask.b,.0001,.9999)-u.detailMasks.x)/(u.detailMasks.y-u.detailMasks.x+.0001),0,1);
  vec3 secondTint=mix(u.detailColorTwo.rgb,u.detailColor.rgb,mix(1,secondMaskRemap,float(u.detailInfo.y)));
  secondAlbedo.rgb*=secondTint;
  float textureMask=mix(mainMask,secondMask.b,float(u.detailInfo2.x));
  textureMask=clamp((clamp(textureMask,.0001,.9999)-u.detailMasks.x)/(u.detailMasks.y-u.detailMasks.x+.0001),0,1);
  float meshMask=mix(vegetationDetail.x,clamp(n.y*.5+.5,0,1),float(u.detailInfo2.y));
  meshMask=clamp((clamp(meshMask,.0001,.9999)-u.detailMasks.z)/(u.detailMasks.w-u.detailMasks.z+.0001),0,1);
  detailBlend=clamp((clamp(textureMask*meshMask,.0001,.9999)-u.detailMaterial.z)/(u.detailMaterial.w-u.detailMaterial.z+.0001),0,1)*u.detailValues.x;
  blendMaskRemapped=mix(blendMaskRemapped,secondMaskRemap,detailBlend);
  vec3 detailRgb=mix(base.rgb*secondAlbedo.rgb*4.594794,secondAlbedo.rgb,float(u.detailInfo.z));
  base.rgb=mix(base.rgb,detailRgb,detailBlend);
  base.a=mix(base.a*secondAlbedo.a,mix(base.a,secondAlbedo.a,detailBlend),float(u.detailInfo.w));
  vec4 packedDetailNormal=texture(detailMaps[1],detailUv);
  secondNormal=vec3(vec2(packedDetailNormal.r*packedDetailNormal.a,packedDetailNormal.g)*2-1,1);
  secondNormal.xy*=u.detailValues.y;
  vec2 detailNormal=u.detailInfo.x==2u?(transpose(normalFrame)*vec3(secondNormal.x,0,secondNormal.y)).xy:secondNormal.xy;
  mappedNormal.xy=mix(mappedNormal.xy,mappedNormal.xy*u.detailColor.w+detailNormal,detailBlend);
  n=safeNormal(normalFrame*mappedNormal,ng);
 }
 if(u.vegetationGradientOne.w>=0) {
  float gradientValue=clamp((vegetationDetail.x-u.vegetationGradientOne.w)/(u.vegetationGradientTwo.w-u.vegetationGradientOne.w+.0001),0,1);
  vec3 gradientColor=mix(u.vegetationGradientTwo.rgb,u.vegetationGradientOne.rgb,gradientValue);
  base.rgb*=mix(vec3(1),gradientColor,blendMaskRemapped);
 }
 float vertexOcclusionMask=clamp((clamp(vegetationFactors.y,.0001,.9999)-u.vegetationOcclusionParams.x)/(u.vegetationOcclusionParams.y-u.vegetationOcclusionParams.x+.0001),0,1);
 base.rgb*=mix(u.vegetationOcclusionColor.rgb,vec3(1),vertexOcclusionMask);
 float meshVariation=clamp(vegetationFactors.x,.01,.99);
 if(u.material.z==1) {
  if(u.extrasInfo.w!=0u && u.extrasInfo.z!=0u) {
   float variation=mix(0,meshVariation,u.extrasUsage2.z);
   float detailFadeMask=mix(1-detailBlend,1,u.extrasUsage2.w);
   float globalAlpha=mix(1,clamp(clamp(globalExtras.a,0,1)-clamp(variation,0,1)+clamp(globalExtras.a,0,1)*.1,0,1),detailFadeMask*u.extrasUsage2.y);
   vec3 derivativeNormal=safeNormal(cross(dFdy(worldPosition),dFdx(worldPosition)),ng);
   vec3 viewDirection=safeNormal(u.camera.xyz-worldPosition,vec3(0,0,1));
   float fadeGlancing=mix(1,clamp(abs(dot(viewDirection,derivativeNormal))*3,0,1),u.vegetationAlphaFade.x);
   float cameraRange=u.vegetationAlphaCamera.y-u.vegetationAlphaCamera.x;
   float fadeCamera=mix(1,clamp((distance(worldPosition,u.camera.xyz)-u.vegetationAlphaCamera.x)/(cameraRange+.0001),0,1),u.vegetationAlphaFade.y);
   float fadeEffects=mix(1,fadeGlancing*fadeCamera*(1-u.vegetationAlphaFade.z),detailFadeMask);
   float fadeAlpha=clamp(fadeEffects-texture(vegetationFadeNoise,u.vegetationAlphaFade.w*worldPosition).r,0,1);
   float preclip=min((base.a-u.material.w)*globalAlpha,fadeAlpha);
   if(preclip<.01) discard;
  } else if(base.a<u.material.w) discard;
 }
 vec4 globalColors=u.vegetationFieldColor;
 if(u.colorsInfo.z!=0u) {
  vec3 samplePosition=u.colorsInfo.y!=0u?u.model[3].xyz:worldPosition;
  vec2 colorsUv=u.colorsCoords.zw+u.colorsCoords.xy*samplePosition.xz;
  float usage=u.colorsInfo.x<4u?u.colorsUsage0[u.colorsInfo.x]:(u.colorsInfo.x<8u?u.colorsUsage1[u.colorsInfo.x-4u]:u.colorsUsage2.x);
  globalColors=usage>=.5?textureLod(colorsMap,vec3(colorsUv,float(u.colorsInfo.x)),0):u.colorsFallback;
 }
 float colorsInfluence=clamp(globalColors.a,0,1);
 float baseGray=dot(base.rgb,vec3(.2126,.7152,.0722));
 vec3 colorsBase=mix(base.rgb,vec3(baseGray),1-(1-colorsInfluence)*(1-colorsInfluence));
 float colorsMaskRemapped=clamp((clamp(mainMask,.0001,.9999)-u.colorMaskParams.y)/(u.colorMaskParams.z-u.colorMaskParams.y+.0001),0,1);
 float colorsValue=mix(1,colorsMaskRemapped,u.vegetationColorParams.z)*u.vegetationColorParams.x;
 float colorsVariation=mix(1,meshVariation,u.vegetationColorParams.w);
 float colorsOcclusion=vertexOcclusionMask;
 if(u.vegetationInfo.w!=0u) colorsOcclusion=1-colorsOcclusion;
 colorsOcclusion=mix(u.vegetationStageParams.z,1,colorsOcclusion);
 float colorsShading=clamp(clamp(dot(base.rgb,vec3(.2126,.7152,.0722))*5,0,1),.2,1);
 float colorsBlend=clamp((colorsValue*colorsInfluence*colorsVariation*colorsOcclusion*colorsShading-u.vegetationGlobalMasks.x)/(u.vegetationGlobalMasks.y-u.vegetationGlobalMasks.x+.0001),0,1);
 vec3 colorsRgb=globalColors.rgb*4.594794*u.vegetationColorParams.y;
 base.rgb=mix(base.rgb,colorsBase*colorsRgb,colorsBlend);
 float overlayVariation=mix(1,meshVariation,u.vegetationStageParams.x);
 float overlayValue=u.vegetationOverlay.a*globalExtras.b*overlayVariation;
 float overlayProjection=mix(1,clamp(n.y,0,1),u.vegetationStageParams.y);
 float overlayOcclusion=u.vegetationInfo.z!=0u ? 1-vertexOcclusionMask : vertexOcclusionMask;
 overlayOcclusion=mix(u.vegetationStageParams.z,1,overlayOcclusion);
 float overlayShading=clamp(clamp(dot(base.rgb,vec3(.2126,.7152,.0722))*5,0,1),.2,1);
 float overlayMask=clamp((overlayValue*overlayProjection*overlayShading*overlayOcclusion-u.vegetationGlobalMasks.z)/(u.vegetationGlobalMasks.w-u.vegetationGlobalMasks.z+.0001),0,1);
 float vegetationNormalScale=mix(1,u.vegetationWetness.y,overlayMask)*mix(1,u.vegetationWetness.z,u.vegetationWetness.x*u.vegetationWetness.x);
 mappedNormal.xy*=vegetationNormalScale;
 vec3 lightingNormal=mappedNormal;
 if(!gl_FrontFacing) {
  uint mode=uint(u.vegetationOcclusionColor.w+.5);
  lightingNormal*=mode==0u?vec3(-1):mode==1u?vec3(-1,-1,1):vec3(1);
 }
 n=safeNormal(normalFrame*lightingNormal,ng);
 base.rgb=mix(base.rgb,u.vegetationOverlay.rgb,overlayMask);
 float globalWetness=u.vegetationWetness.x*globalExtras.g;
 base.rgb=mix(base.rgb,base.rgb*base.rgb,u.vegetationStageParams.w*globalWetness);
 vec3 wetnessStageAlbedo=base.rgb;
 base.rgb*=vec3(1)+u.motionHighlightColor.rgb*motionHighlight;
 if(u.misc.z>0) { outColor=vec4(u.material.z==3?base.rgb*base.a:base.rgb,u.material.z>=2?base.a:1); return; }
 float metallic=u.material.x,roughness=u.material.y;
 if(u.uvInfo[1].present>0) { vec4 s=texture(maps[1],texcoord01.zw); metallic*=s.b; roughness*=s.g; }
 if(u.detailValues.x>0) {
  metallic=mix(metallic,secondMask.r*u.detailValues.w,detailBlend);
  roughness=mix(roughness,1-secondMask.a*u.detailMaterial.y,detailBlend);
 }
 metallic=mix(metallic,0,overlayMask);
 roughness=mix(roughness,1-u.vegetationWetness.w,overlayMask);
 roughness=max(roughness-(2*globalWetness-globalWetness*globalWetness),0);
 roughness=clamp(roughness,.045,1);
 float ao=1;
 if(u.uvInfo[3].present>0) ao=mix(1,texture(maps[3],texcoord23.zw).r,u.misc.y);
 if(u.detailValues.x>0) ao=mix(ao,mix(1,secondMask.g,u.detailMaterial.x),detailBlend);
 vec3 emission=u.emissive.rgb*u.emissive.w;
 if(u.uvInfo[4].present>0) {
  vec3 c=texture(maps[4],texcoord45.xy).rgb;c=u.uvInfo[4].srgb>0?linearColor(c):c;
  if(u.vegetationEmission.w>=0) {
   c=clamp((c-vec3(u.vegetationEmission.x))/(u.vegetationEmission.y-u.vegetationEmission.x+.0001),0,1);
   float field=mix(1,globalExtras.r,u.vegetationEmission.w);
   c=clamp(c+field*u.vegetationEmission.z-1,0,1);
  }
  emission*=c;
 }
 if(u.vegetationEmission.w<0) emission*=globalExtras.r;
 float specWeight=u.specular.w;
 if(u.uvInfo[5].present>0) specWeight*=texture(maps[5],texcoord45.zw).a;
 vec3 specColor=u.specular.rgb;
 if(u.uvInfo[6].present>0) {vec3 c=texture(maps[6],texcoord67.xy).rgb;specColor*=u.uvInfo[6].srgb>0?linearColor(c):c;}
 float ior=u.coat.w;
 float dielectric=ior==0?1:pow((ior-1)/(ior+1),2);
 vec3 f0=mix(min(vec3(dielectric)*specColor,vec3(1))*specWeight,base.rgb,metallic);
 vec3 f90=vec3(mix(specWeight,1,metallic));
 vec3 v=safeNormal(u.camera.xyz-worldPosition,n);
 mat3 bn=basis(n,texcoord23.xy);
 float strength=u.ambient.w,angle=u.camera.w;
 vec2 direction=vec2(cos(angle),sin(angle));
 if(u.uvInfo[7].present>0) { vec3 a=texture(maps[7],texcoord67.zw).rgb;vec2 d=a.rg*2-1;
  d=dot(d,d)>1e-10?normalize(d):vec2(1,0);direction=mat2(direction.x,direction.y,-direction.y,direction.x)*d;strength*=a.b; }
 vec3 t=safeNormal(bn*vec3(direction,0),bn[0]),b=safeNormal(cross(n,t),bn[1]);
 float ab=roughness*roughness,at=mix(ab,1,strength*strength);
 float coat=u.coat.x,cr=u.coat.y;
 if(u.uvInfo[8].present>0) coat*=texture(maps[8],texcoord89.xy).r;
 if(u.uvInfo[9].present>0) cr*=texture(maps[9],texcoord89.zw).g;
 vec3 cn=ng;
 if(u.uvInfo[10].present>0) {vec3 s=texture(maps[10],texcoord10).xyz*2-1;s.xy*=u.coat.z;cn=safeNormal(basis(ng,texcoord10)*s,ng);}
 mat3 cb=basis(cn,texcoord10);float ca=max(cr*cr,.002025);
 vec3 color=vec3(0);float vis=visibility();
 vec3 translucency=u.translucencyColor.rgb*wetnessStageAlbedo*u.translucencyColor.a*u.translucencyLighting.w*
                   mix(1,u.motionHighlightColor.a,overlayValue)*10;
 if(u.translucencyLighting.z>0 && u.translucencyColor.a>0) {
  float mask=texture(maps[1],texcoord01.zw).a;
  float remapped=clamp((clamp(mask,.0001,.9999)-u.translucencyMask.x)/(u.translucencyMask.y-u.translucencyMask.x+.0001),0,1);
  translucency*=mix(1,remapped,u.translucencyLighting.z);
 }
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
  if(u.translucencyColor.a>0) {
   vec3 distorted=l+n*u.translucencyParams.y;
   float viewLobe=pow(clamp(dot(v,-distorted),0,1),u.translucencyParams.z);
   float transShadow=mix(1,u.lights[i].posRadius.w<=0?vis:1,u.translucencyLighting.y);
   color+=base.rgb*translucency*u.translucencyParams.x*u.lights[i].color.rgb*attenuation*transShadow*
          (vec3(viewLobe*u.translucencyParams.w)+u.ambient.rgb*u.translucencyLighting.x);
  }
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
