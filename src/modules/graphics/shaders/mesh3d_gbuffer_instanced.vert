#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 7) in vec4 inMvp0;
layout(location = 8) in vec4 inMvp1;
layout(location = 9) in vec4 inMvp2;
layout(location = 10) in vec4 inMvp3;
layout(location = 11) in vec4 inModelR0;
layout(location = 12) in vec4 inModelR1;
layout(location = 13) in vec4 inModelR2;
layout(location = 14) in vec4 inClip;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) flat out vec4 vClip;

void main() {
    mat4 mvp = mat4(inMvp0, inMvp1, inMvp2, inMvp3);
    vec4 hp = mvp * vec4(inPos, 1.0);
    gl_Position = hp;
    mat3 nrmMat = mat3(inModelR0.x, inModelR1.x, inModelR2.x,
                      inModelR0.y, inModelR1.y, inModelR2.y,
                      inModelR0.z, inModelR1.z, inModelR2.z);
    vWorldNormal = normalize(nrmMat * inNormal);
    vUV = inUV;
    vClip = inClip;
}
