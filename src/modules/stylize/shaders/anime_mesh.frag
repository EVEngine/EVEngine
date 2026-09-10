#version 450
#extension GL_GOOGLE_include_directive : enable
#include "anime_shadow.glsl"
// Character material: preserve authored albedo; antialias the light terminator.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vLightDir;
layout(location=4) in vec3 vLightColor;
layout(location=5) in vec3 vWorldPos;
layout(location=6) in vec3 vCameraPos;
layout(set=0,binding=1) uniform sampler2D albedo;
layout(push_constant) uniform Params { float data[32]; } u;
layout(location=0) out vec4 outColor;
void main() {
    vec4 base=texture(albedo,vUV)*vTint;
    vec3 N=normalize(vNormal), L=normalize(vLightDir);
    vec3 V=normalize(vCameraPos-vWorldPos);
    float skin=u.data[4], hair=u.data[5];
    // Optional art-directed skin relighting; default preserves the authored atlas.
    base.rgb=mix(base.rgb,mix(vec3(1.0,0.88,0.79),base.rgb,clamp(u.data[6],0.0,1.0)),skin);
    float ndl=dot(N,L);
    float width=max(fwidth(ndl)*1.5,max(u.data[1],0.001));
    float lit=smoothstep(u.data[0]-width,u.data[0]+width,ndl);
    lit=min(lit,mix(1.0,animeShadow(vWorldPos,N),clamp(u.data[15],0.0,1.0)));
    vec3 shadow=mix(vec3(u.data[9],u.data[10],u.data[11]),vec3(0.88,0.68,0.65),skin);
    shadow=mix(vec3(1.0),shadow,clamp(u.data[2],0.0,1.0));
    vec3 color=base.rgb*mix(shadow,vec3(1.0,0.985,0.975),lit);
    color*=mix(1.0-clamp(u.data[12],0.0,1.0),1.0,clamp(ndl*0.5+0.5,0.0,1.0));
    vec3 halfVector=L+V;
    vec3 H=halfVector*inversesqrt(max(dot(halfVector,halfVector),0.000001));
    vec3 strand=vec3(0.0,1.0,0.0)-N*N.y;
    strand=normalize(strand+vec3(0.001,0.0,0.0));
    float th=dot(strand,H);
    float spec=pow(max(1.0-th*th,0.0),max(u.data[8],1.0));
    float highlight=smoothstep(0.40,0.70,spec)*hair*lit
        *smoothstep(0.15,0.65,dot(N,H));
    color+=vec3(1.0,0.52,0.45)*u.data[7]*highlight;
    float surfaceSpec=pow(max(dot(N,H),0.0),max(u.data[8],1.0));
    color+=mix(vec3(1.0),base.rgb,0.65)*u.data[14]*lit*smoothstep(0.25,0.65,surfaceSpec);
    float rim=pow(1.0-max(dot(N,V),0.0),4.0);
    rim*=smoothstep(-0.2,0.5,ndl)*(1.0-skin*0.65);
    color+=vec3(0.63,0.79,1.0)*u.data[3]*rim;
    color*=clamp(vLightColor,vec3(0.0),vec3(2.0));
    color=mix(color,base.rgb,clamp(u.data[13],0.0,1.0));
    outColor=vec4(color,base.a);
}
