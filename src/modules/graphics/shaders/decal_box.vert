#version 450

layout(push_constant) uniform DecalPush {
    mat4 model;
    vec4 uvRect;      // atlas region [x, y, w, h]
    vec4 fadeParams;  // x = fade, y = normalStrength, z = roughStrength, w = metalStrength
    vec4 extraParams; // x = emissiveStrength, y = blendMode, z/w unused
} decal;

layout(location = 0) flat out vec4 vUV;
layout(location = 1) flat out vec4 vFade;
layout(location = 2) flat out vec4 vExtra;

void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    vUV = decal.uvRect;
    vFade = decal.fadeParams;
    vExtra = decal.extraParams;
}
