#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#include "pt_scene.glsl"

layout(location = 0) rayPayloadInEXT PtPayload payload;
hitAttributeEXT vec2 barycentric;

void main() {
    payload.barycentric = barycentric;
    payload.distance = gl_HitTEXT;
    payload.instance = gl_InstanceCustomIndexEXT;
    payload.primitive = gl_PrimitiveID;
}
