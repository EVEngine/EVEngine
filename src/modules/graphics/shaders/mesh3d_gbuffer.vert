#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 5) in vec4 inColor;

// Packed to fit Vulkan's 128-byte push-constant guarantee:
// mvp (64) + 3 inverse-transpose normal rows (48) + clip (16) = 128.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 modelR0; // normal-matrix row 0
    vec4 modelR1; // normal-matrix row 1
    vec4 modelR2; // normal-matrix row 2
    vec4 clip;    // x=near, y=far
} pc;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vColor;

void main() {
    vec4 hp = pc.mvp * vec4(inPos, 1.0);
    gl_Position = hp;

    // The inverse-transpose is computed once per draw on the CPU.
    mat3 nrmMat = mat3(pc.modelR0.x, pc.modelR1.x, pc.modelR2.x, pc.modelR0.y, pc.modelR1.y,
                       pc.modelR2.y, pc.modelR0.z, pc.modelR1.z, pc.modelR2.z);
    vWorldNormal = normalize(nrmMat * inNormal);
    vUV = inUV;
    vColor = inColor;
}
