#version 450
#extension GL_GOOGLE_include_directive : enable
#include "shadow.glsl"
layout(location=0) in vec3 normal;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 tint;
layout(location=3) in vec3 world;
layout(location=4) in vec3 camera;
layout(location=5) in vec3 viewPosition;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;vec4 tint;
    vec4 cameraPos;vec4 ambient;
} frame;
layout(push_constant) uniform Parameters {float waterLevel;} p;
layout(location=0) out vec4 outColor;
float hash(vec3 q) {return fract(sin(dot(q,vec3(127.1,311.7,74.7)))*43758.5453);}
float noise(vec3 q) {
    vec3 i=floor(q),f=fract(q);f=f*f*(3-2*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),
                   mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
                   mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void main() {
    vec3 N=normalize(normal),L=normalize(frame.lightDirIntensity.xyz),V=normalize(camera-world);
    float mottling=noise(world*4.0)*.55+noise(world*13.0)*.30+noise(world*53.0)*.15;
    vec3 base=mix(vec3(.10,.115,.11),vec3(.28,.29,.265),mottling);
    float wet=1-smoothstep(p.waterLevel-.05,p.waterLevel+.17,world.y);
    base*=mix(1.0,.47,wet);
    vec3 lit=base*(frame.ambient.rgb*.65+frame.lightColor.rgb*max(dot(N,L),0.0)*.62*sceneShadow(world,N,-viewPosition.z));
    float glint=pow(max(dot(N,normalize(V+L)),0.0),48.0)*wet*.22;
    outColor=vec4((lit+glint*frame.lightColor.rgb)*tint.rgb,1.0);
}
