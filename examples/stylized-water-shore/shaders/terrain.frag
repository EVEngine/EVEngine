#version 450
#extension GL_GOOGLE_include_directive : enable
#include "shadow.glsl"
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D {vec4 posRadius;vec4 color;vec4 spot;};
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;vec4 tint;
    vec4 cameraPos;vec4 ambient;
} frame;
layout(set=0,binding=1) uniform sampler2D sand;
// Fine river-bed pebbles are evaluated in world space, independently of mesh tessellation.
layout(push_constant) uniform Parameters {float waterLevel;} p;
layout(location=0) out vec4 outColor;
float hash(vec2 q) {return fract(sin(dot(q,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 q) {
    vec2 i=floor(q),f=fract(q);f=f*f*(3-2*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
void main() {
    vec2 q=vWorldPos.xz;
    vec2 rotated=mat2(.91,.41,-.41,.91)*q;
    float pattern=noise(rotated*.8)*.33+noise(rotated*2.21)*.26
        +noise(rotated*6.73)*.23+noise(rotated*18.37)*.18;
    float grassMask=smoothstep(.15,.36,vWorldPos.y-p.waterLevel+(pattern-.5)*.14);
    vec3 beach=texture(sand,q*.5).rgb*(.86+.20*pattern);
    vec3 meadow=vec3(.085,.125,.035)*(.84+.32*pattern);
    float wet=1-smoothstep(-.02,.16,vWorldPos.y-p.waterLevel);
    beach*=mix(1.0,.66,wet);
    vec3 albedo=mix(beach,meadow,grassMask);
    vec3 N=normalize(vNormal),L=normalize(frame.lightDirIntensity.xyz);
    float light=max(dot(N,L),0.0)*sceneShadow(vWorldPos,N,-vViewPos.z);
    vec3 lit=albedo*(frame.ambient.rgb*.65+frame.lightColor.rgb*light*.68);
    outColor=vec4(lit*vTint.rgb,1.0);
}
