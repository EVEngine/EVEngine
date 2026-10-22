#version 450
#extension GL_GOOGLE_include_directive : enable
#include "wave.glsl"
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
// x: signed distance to the authored water boundary; y: local bed depth.
layout(location=2) in vec2 inUV;
struct Light3D { vec4 posRadius; vec4 color; vec4 spot; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind; vec4 bindlessEnv;
    vec4 envProbeCenter; vec4 envProbeExtent; vec4 skinInfo;
    vec4 reflectionProbeCenter[2]; vec4 reflectionProbeExtent[2];
} frame;
layout(push_constant) uniform Params { float data[32]; } p;
layout(location=0) out vec3 normal;
layout(location=1) out vec2 shore;
layout(location=2) out vec4 tint;
layout(location=3) out vec3 world;
layout(location=4) out vec3 camera;
layout(location=5) out vec3 viewPosition;

void main() {
    vec3 pos=inPos;
    vec3 n=inNormal;
#ifndef INTERACTION
    pos.y+=shoreGeometryHeight(inUV.x,pos.xz,shoreWrappedTime(p.data[0]*p.data[1]),p.data[2]);
#endif
    vec4 w=frame.model*vec4(pos,1);
    gl_Position=frame.mvp*vec4(pos,1);
    normal=normalize(transpose(inverse(mat3(frame.model)))*n);
    shore=inUV;
#ifdef INTERACTION
    shore.y=max(0.0,inUV.y+pos.y-0.55);
#endif
    tint=frame.tint; world=w.xyz; camera=frame.cameraPos.xyz;
    viewPosition=(frame.view*w).xyz;
}
