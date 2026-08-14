#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// Packed to fit Vulkan's 128-byte push-constant guarantee:
// mvp (64) + 3 model rows with translation in .w (48) + clip (16) = 128.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 modelR0; // m00,m01,m02, tx
    vec4 modelR1; // m10,m11,m12, ty
    vec4 modelR2; // m20,m21,m22, tz
    vec4 clip;    // x=near, y=far
} pc;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out float vNdcZ;
layout(location = 2) out vec2 vUV;

void main() {
    vec4 hp = pc.mvp * vec4(inPos, 1.0);
    gl_Position = hp;

    // Upper-left 3x3 of column-major model; inverse-transpose for non-uniform scale.
    mat3 nrmMat = mat3(pc.modelR0.x, pc.modelR1.x, pc.modelR2.x, pc.modelR0.y, pc.modelR1.y,
                       pc.modelR2.y, pc.modelR0.z, pc.modelR1.z, pc.modelR2.z);
    nrmMat = transpose(inverse(nrmMat));
    vWorldNormal = normalize(nrmMat * inNormal);
    vNdcZ = hp.z / max(hp.w, 1e-6);
    vUV = inUV;
}
