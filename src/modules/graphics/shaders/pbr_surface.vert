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
 vec4 vertexCoords; vec4 vertexFallback; vec4 vertexUsage0; vec4 vertexUsage1; vec4 vertexUsage2;
 vec4 vertexSize;
 uvec4 vertexInfo;
 vec4 motionCoords; vec4 motionFallback; vec4 motionUsage0; vec4 motionUsage1; vec4 motionUsage2;
 vec4 motionGlobal0; vec4 motionGlobal1; vec4 motionTime; vec4 motionBending; vec4 motionBranch; vec4 motionBranch2;
 vec4 motionFlutter; vec4 motionControl; vec4 motionPerspective;
 uvec4 motionInfo;
} u;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in uvec4 joints;
layout(location=4) in vec4 weights;
layout(location=5) in vec2 sourceUv0;
layout(location=6) in vec2 sourceUv1;
layout(location=7) in vec2 sourceUv2;
layout(location=8) in vec2 sourceUv3;
layout(location=9) in vec2 sourceUv4;
layout(location=10) in vec2 sourceUv5;
layout(location=11) in vec2 sourceUv6;
layout(location=12) in vec2 sourceUv7;
layout(location=13) in vec2 sourceUv8;
layout(location=14) in vec2 sourceUv9;
layout(location=15) in vec2 sourceUv10;
layout(set=0,binding=3,std430) readonly buffer Skin { mat4 values[]; } skin;
layout(set=0,binding=2,std430) readonly buffer Tangents { float values[]; } tangentStream;
layout(set=0,binding=10) uniform sampler2DArray vertexField;
layout(set=0,binding=11) uniform sampler2DArray motionField;
layout(set=0,binding=12) uniform sampler2D motionNoise;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec4 texcoord01;
layout(location=3) out vec4 texcoord23;
layout(location=4) out vec4 texcoord45;
layout(location=5) out vec4 texcoord67;
layout(location=6) out vec4 texcoord89;
layout(location=7) out vec2 texcoord10;
layout(location=8) out vec2 normalBasisUv;
layout(location=9) out vec3 worldTangent;
layout(location=10) out vec3 worldBitangent;
layout(location=11) out float motionHighlight;
layout(location=12) out vec2 vegetationFactors;
layout(location=13) out vec3 vegetationDetail;
vec2 materialUv(int slot, vec2 source) {
 vec2 t=texcoord;
 if(u.uvInfo[slot].offset!=0xffffffffu)
  t=source;
 t*=u.uvTransform[slot].zw;
 float c=cos(u.uvInfo[slot].rotation),s=sin(u.uvInfo[slot].rotation);
 return mat2(c,s,-s,c)*t+u.uvTransform[slot].xy;
}
vec3 rotateAxis(vec3 value,vec3 axis,float angle) {
 vec3 onAxis=axis*dot(axis,value),other=value-onAxis;
 return onAxis+other*cos(angle)+cross(axis,other)*sin(angle);
}
void main() {
 motionHighlight=u.tangentInfo.w!=0u ? tangentStream.values[u.tangentInfo.z+uint(gl_VertexIndex)] : 0;
 uint vegetationAt=u.vegetationInfo.x+uint(gl_VertexIndex)*5u;
 vegetationFactors=u.vegetationInfo.y!=0u ? vec2(tangentStream.values[vegetationAt],tangentStream.values[vegetationAt+1]) : vec2(1);
 vegetationDetail=u.vegetationInfo.y!=0u ? vec3(tangentStream.values[vegetationAt+2],tangentStream.values[vegetationAt+3],tangentStream.values[vegetationAt+4]) : vec3(1,texcoord);
 mat4 pose=mat4(1);
 if(u.misc.w>0) {
  pose=mat4(0); float total=0;
  for(int i=0;i<4;i++) if(weights[i]>0 && joints[i]<uint(u.misc.w)) {
   pose+=weights[i]*skin.values[joints[i]]; total+=weights[i];
  }
  pose=total>0 ? pose/total : mat4(1);
 }
 vec4 p=pose*vec4(position,1);
 if(u.motionInfo.y==1u) {
  uint at=u.vertexInfo.z+uint(gl_VertexIndex)*9u;
  vec3 localPivot=vec3(tangentStream.values[at],tangentStream.values[at+1],tangentStream.values[at+2]);
  float bendingMask=tangentStream.values[at+3];
  float branchMask=tangentStream.values[at+4];
  float flutterMask=tangentStream.values[at+5];
  float authoredVariation=tangentStream.values[at+6];
  float boundsRadius=tangentStream.values[at+8];
  vec3 world=(u.model*p).xyz;
  vec3 objectWorld=(u.model*vec4(localPivot,1)).xyz;
  vec3 origin=vec3(u.motionGlobal0.zw,u.motionGlobal1.x);
  vec3 shifted=world-origin,objectShifted=objectWorld-origin;
  float hashValue=sin(objectShifted.x*12.9898+objectShifted.z*78.233);
  float variation=clamp(fract(hashValue*(1-u.motionGlobal1.y)+authoredVariation),.01,.99);
  vec3 noisePosition=mix(shifted,objectShifted,u.motionGlobal1.z);
  vec2 noiseUv=noisePosition.xz*((u.motionBending.z+.2)*u.motionControl.x*.0075);
  float flowTime=(u.motionTime.x*u.motionBending.y+u.motionBending.w*variation)*.03;
  float cycle=fract(flowTime);
  vec2 globalDirection=u.motionGlobal0.xy;
  vec4 noiseValue=mix(texture(motionNoise,noiseUv-globalDirection*cycle),
                      texture(motionNoise,noiseUv-globalDirection*fract(flowTime+.5)),abs(cycle-.5)/.5);
  mat3 inverseModel=inverse(mat3(u.model));
  vec3 parentScale;
  for(int i=0;i<3;i++) parentScale[i]=1/length(vec3(inverseModel[0][i],inverseModel[1][i],inverseModel[2][i]));
  vec3 noiseDirection=(inverseModel*vec3(noiseValue.r*2-1,0,noiseValue.g*2-1))*parentScale;
  vec3 windDirection=(inverseModel*vec3(globalDirection.x,0,globalDirection.y))*parentScale;
  float usage=u.motionInfo.x<4u ? u.motionUsage0[u.motionInfo.x] :
              (u.motionInfo.x<8u ? u.motionUsage1[u.motionInfo.x-4u] : u.motionUsage2.x);
  vec2 fieldUv=u.motionCoords.zw+u.motionCoords.xy*objectWorld.xz;
  vec4 field=usage>=.5 ? textureLod(motionField,vec3(fieldUv,float(u.motionInfo.x)),0) : u.motionFallback;
  vec3 reactDirection=(inverseModel*vec3(field.x,0,field.y))*parentScale;
  float rawPower=clamp(field.z,0,1),power=1-(1-rawPower)*(1-rawPower);
  vec3 bendDirection=mix(noiseDirection,windDirection,power*.6);
  vec3 bending=bendDirection*(2*bendingMask*u.motionBending.x*power*power*u.motionBranch2.y);
  float interactionRemap=clamp(clamp(bendingMask,.0001,.9999)/(u.motionControl.z+.0001),0,1);
  vec3 interaction=reactDirection*(2*interactionRemap*u.motionControl.y);
  float interactionWeight=clamp(u.motionControl.y*pow(clamp(field.w,0,1),4),0,1);
  vec3 angles=mix(bending,interaction,interactionWeight);
  float branchPhase=(shifted.x+shifted.y+shifted.z)*u.motionBranch.w*.1;
  float sineA=sin(branchPhase+u.motionBranch2.x*variation+u.motionTime.x*u.motionBranch.z);
  float sineB=sin(u.motionTime.x*u.motionBranch.z*.6842+branchPhase);
  float localLength=length(p.xyz);
  vec3 normalPosition=localLength>1e-8 ? p.xyz/localLength : vec3(0);
  float facing=max(mix(1,dot(normalPosition,-reactDirection)*.5+.5,u.motionGlobal1.w),.001);
  float noiseBlue=abs(noiseValue.b);
  float branchAmplitude=facing*power*pow(noiseBlue,mix(1.8,.4,power));
  float squashScale=(max(sineA,sineB)*.5+.5)*branchAmplitude*u.motionBranch.x*branchMask*
                    boundsRadius*u.motionBranch2.z;
  vec3 squash=vec3(reactDirection.x,sineA*.3,reactDirection.z)*squashScale;
  float rolling=sineA*branchAmplitude*u.motionBranch.y*branchMask*u.motionBranch2.z;
  vec2 flutterUv=shifted.xz*(u.motionFlutter.z*.03)+
                 vec2(u.motionFlutter.w*variation+u.motionTime.x*u.motionFlutter.y*.02);
  vec3 flutterNoise=texture(motionNoise,flutterUv).rgb*2-1;
  float fadeEnd=u.motionControl.w+.01;
  float fade=clamp((distance(world,u.camera.xyz)-fadeEnd)/(-fadeEnd*.5+.0001),0,1);
  float flutterAmplitude=u.motionFlutter.x*facing*fade*flutterMask*power*u.motionBranch2.w*
                         pow(noiseBlue,mix(2.4,.6,power));
  vec3 result=p.xyz-localPivot;
  result=rotateAxis(result,vec3(1,0,0),angles.z);
  result=rotateAxis(result,vec3(0,0,1),-angles.x);
  result=rotateAxis(result+squash,vec3(0,1,0),rolling)+flutterNoise*flutterAmplitude;
  vec3 viewDirection=u.camera.xyz-world;
  float viewLength=length(viewDirection);
  viewDirection=viewLength>1e-8 ? viewDirection/viewLength : vec3(0);
  vec3 crossView=cross(viewDirection,vec3(0,1,0));
  vec3 push=inverseModel*vec3(-crossView.z,0,crossView.x)*u.motionPerspective.x;
  vec3 jitter=vec3(variation*2-1,0,variation*2-1)*u.motionPerspective.y;
  result+=(push+jitter)*bendingMask*pow(abs(viewDirection.y),u.motionPerspective.z);
  p.xyz=result+localPivot;
  motionHighlight=abs(noiseValue.a)*power*fade*bendingMask;
 }
 if(u.vertexInfo.y==2u) {
  vec3 localPivot=vec3(0);
  if(u.vertexInfo.w!=0u) {
   uint deformationAt=u.vertexInfo.z+uint(gl_VertexIndex)*9u;
   localPivot=vec3(tangentStream.values[deformationAt],tangentStream.values[deformationAt+1],
                   tangentStream.values[deformationAt+2]);
  }
  vec3 pivot=(u.model*vec4(localPivot,1)).xyz;
  vec2 uv=u.vertexCoords.zw+u.vertexCoords.xy*pivot.xz;
  float usage=u.vertexInfo.x<4u ? u.vertexUsage0[u.vertexInfo.x] :
              (u.vertexInfo.x<8u ? u.vertexUsage1[u.vertexInfo.x-4u] : u.vertexUsage2.x);
  vec4 field=usage>=.5 ? textureLod(vertexField,vec3(uv,float(u.vertexInfo.x)),0) : u.vertexFallback;
  float size=mix(1,clamp(field.a,0,1),u.vertexSize.x);
  float distanceFade=clamp((distance(u.camera.xyz,pivot)/u.vertexSize.w-u.vertexSize.z)/
                           (u.vertexSize.y-u.vertexSize.z+.0001),0,1);
  p.xyz=(p.xyz-localPivot)*size*distanceFade+localPivot;
 }
 worldPosition=(u.model*p).xyz;
 worldNormal=transpose(inverse(mat3(u.model*pose)))*normal;
 worldTangent=vec3(0); worldBitangent=vec3(0);
 if(u.tangentInfo.y!=0u) {
  uint at=u.tangentInfo.x+uint(gl_VertexIndex)*6u;
  vec3 t=vec3(tangentStream.values[at],tangentStream.values[at+1],tangentStream.values[at+2]);
  vec3 b=vec3(tangentStream.values[at+3],tangentStream.values[at+4],tangentStream.values[at+5]);
  worldTangent=mat3(u.model*pose)*t;
  worldBitangent=mat3(u.model*pose)*b;
 }
 gl_Position=u.mvp*p;
 texcoord01=vec4(materialUv(0,sourceUv0),materialUv(1,sourceUv1));
 texcoord23=vec4(materialUv(2,sourceUv2),materialUv(3,sourceUv3));
 texcoord45=vec4(materialUv(4,sourceUv4),materialUv(5,sourceUv5));
 texcoord67=vec4(materialUv(6,sourceUv6),materialUv(7,sourceUv7));
 texcoord89=vec4(materialUv(8,sourceUv8),materialUv(9,sourceUv9));
 texcoord10=materialUv(10,sourceUv10);
 normalBasisUv=u.uvInfo[2].offset==0xffffffffu ? texcoord : sourceUv2;
}
