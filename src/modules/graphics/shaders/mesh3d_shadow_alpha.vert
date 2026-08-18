#version 450
// Alpha-tested shadow caster vertex: identical transform to mesh3d_shadow.vert,
// plus a UV passthrough so the fragment can discard transparent texels.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

layout(location = 0) out vec2 vUV;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vUV = inUV;
}
