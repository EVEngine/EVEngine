#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#include "pt_scene.glsl"

layout(location = 0) rayPayloadInEXT PtPayload payload;

void main() {
    payload.instance = 0xffffffffu;
}
