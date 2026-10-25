#version 450
#extension GL_GOOGLE_include_directive : enable
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTangentTransport;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(set=0,binding=1) uniform sampler2D albedoMap;
layout(set=0,binding=2) uniform sampler2D normalMap;
layout(set=0,binding=6) uniform sampler2D terrainWeights;
layout(location=0) out vec4 outColor;
#include "BundledParams.inc"
#if SHADOWS
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3];vec4 splits;vec4 bias;vec4 cascadeBias;vec4 cascadeTexel;
    mat4 localVP[4];vec4 localSlot01;vec4 localSlot23;vec4 localBias;vec4 localMeta;
} shadow;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;
float shadowVisibility() {
    if(shadow.bias.y<.5 || shadow.bias.z<.5)return 1.0;
    float depth=max(-vViewPos.z,0.0);
    int cascade=depth<shadow.splits.x ? 0 : depth<shadow.splits.y ? 1 : 2;
    vec4 clip=shadow.lightVP[cascade]*vec4(vWorldPos,1);
    vec3 ndc=clip.xyz/clip.w;vec2 uv=ndc.xy*.5+.5;
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) || ndc.z<0 || ndc.z>1)return 1.0;
    vec2 texel=1.0/vec2(textureSize(shadowMap,0).xy);
    float bias=max(shadow.cascadeBias[cascade],shadow.bias.x);
    float visibility=0.0;
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)
        visibility+=texture(shadowMap,vec4(uv+vec2(x,y)*texel,float(cascade),ndc.z-bias));
    return mix(1.0,visibility/9.0,shadow.splits.w);
}
#endif
vec3 decode(vec3 c) { return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),greaterThan(c,vec3(.04045))); }
void main() {
    vec2 geometryUV=vUV*UV_SCALE+UV_OFFSET;
    vec2 uv=geometryUV;
    uv.y=1.0-uv.y;
    vec4 tex;
#if TERRAIN
    vec2 terrainUV=vec2(vUV.x,1.0-vUV.y);
    vec2 weights=texture(terrainWeights,terrainUV).rg;
    vec2 first=vUV*TERRAIN_SCALE0+TERRAIN_OFFSET0;
    vec2 second=vUV*TERRAIN_SCALE1+TERRAIN_OFFSET1;
    first.y=1.0-first.y;second.y=1.0-second.y;
    tex=texture(albedoMap,first)*weights.r+texture(normalMap,second)*weights.g;
#else
    tex=texture(albedoMap,uv);
#endif
    vec3 albedo=tex.rgb*TINT;
    vec3 N=normalize(vNormal);
    vec3 mapN=vec3(0,0,1);
#if !TERRAIN
    mapN=texture(normalMap,uv).rgb*2.0-1.0;
#endif
    mapN.xy*=NORMAL_SCALE;
    mapN.z=sqrt(max(0.0,1.0-dot(mapN.xy,mapN.xy)));
    vec3 dp1=dFdx(vWorldPos),dp2=dFdy(vWorldPos);
    vec2 duv1=dFdx(geometryUV),duv2=dFdy(geometryUV);
    float determinant=duv1.x*duv2.y-duv2.x*duv1.y;
    vec3 authoredTangent=vTangentTransport.rgb*2.0-1.0;
    if(length(authoredTangent)>.5 && NORMAL_SCALE>0.0) {
        vec3 T=normalize(authoredTangent-N*dot(N,authoredTangent));
        vec3 B=normalize(cross(N,T))*(vTangentTransport.a>.5 ? 1.0 : -1.0);
        N=normalize(T*mapN.x+B*mapN.y+N*mapN.z);
    } else if(abs(determinant)>1e-8 && NORMAL_SCALE>0.0) {
        vec3 T=normalize((dp1*duv2.y-dp2*duv1.y)/determinant);
        T=normalize(T-N*dot(N,T));
        vec3 B=normalize((dp2*duv1.x-dp1*duv2.x)/determinant);
        N=normalize(T*mapN.x+B*mapN.y+N*mapN.z);
    }
    vec3 L=normalize(LIGHT_DIRECTION),V=normalize(vCameraPos-vWorldPos),H=normalize(L+V);
    float nl=max(0.0,dot(N,L)),nv=max(.0001,abs(dot(N,V))),nh=max(0.0,dot(N,H)),lh=max(0.0,dot(L,H));
    float perceptualRoughness=1.0-SMOOTHNESS;
    float roughness=max(.002,perceptualRoughness*perceptualRoughness);
    float a2=roughness*roughness;
    float d=(nh*a2-nh)*nh+1.0;
    float distribution=a2/max(3.14159265*d*d,1e-7);
    float visibility=.5/max(nl*(nv*(1.0-roughness)+roughness)+nv*(nl*(1.0-roughness)+roughness),.00001);
    float specular=sqrt(max(0.0001,visibility*distribution*3.14159265))*nl;
    float fd90=.5+2.0*lh*lh*perceptualRoughness;
    float diffuse=(1.0+(fd90-1.0)*pow(1.0-nl,5.0))*(1.0+(fd90-1.0)*pow(1.0-nv,5.0))*nl;
    vec3 f0=mix(vec3(.2209163),albedo,METALLIC);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-lh,5.0);
    float directVisibility=1.0;
#if SHADOWS
    directVisibility=shadowVisibility();
#endif
#if LAMBERT
    vec3 result=albedo*(AMBIENT+LIGHT_COLOR*nl*directVisibility);
#else
    vec3 result=albedo*(1.0-METALLIC)*.7790837*(AMBIENT*OCCLUSION+LIGHT_COLOR*diffuse*directVisibility)+specular*LIGHT_COLOR*fresnel*directVisibility;
#endif
    outColor=vec4(decode(clamp(result,0.0,1.0)),tex.a);
}
