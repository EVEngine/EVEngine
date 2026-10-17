#version 450
#extension GL_GOOGLE_include_directive : enable
#define float2 vec2
#define float3 vec3
#define frac fract
#define ROAD_SAMPLE(t,u) textureLod(t,u,0.0)
layout(location=0) in vec3 vNormal;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(set=0,binding=1) uniform sampler2D roadColor;
layout(set=0,binding=2) uniform sampler2D roadNormal;
layout(location=0) out vec4 outColor;
#include "RoadMaterialParams.inc"
#include "RoadAsphaltCommon.inc"
void main() { outColor=vec4(roadShade(vWorldPos,vNormal,vCameraPos),1.0); }
