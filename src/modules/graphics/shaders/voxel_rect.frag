#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in flat uint vTex;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec4 vTint;
layout(location = 4) in float vAO;
layout(location = 5) in flat float vTiles;

layout(set = 0, binding = 0) uniform sampler2D atlasSampler;

layout(location = 0) out vec4 outColor;

void main() {
    float tileU = 1.0 / vTiles;
    float col = mod(float(vTex), vTiles);
    float row = floor(float(vTex) / vTiles);
    vec2 atlasUV = (vec2(col, row) + fract(vUV)) * tileU;
    // Derivatives must be taken before fract() so mip selection stays stable at
    // every voxel boundary inside a merged rectangle.
    vec4 albedo = textureGrad(atlasSampler, atlasUV, dFdx(vUV) * tileU, dFdy(vUV) * tileU);
    // Simple Lambert toward +Y-ish key light for readability without a full UBO.
    vec3 L = normalize(vec3(0.35, 1.0, 0.25));
    float ndl = max(dot(normalize(vNormal), L), 0.0);
    float shade = 0.35 + 0.65 * ndl;
    // Vertex ambient occlusion (0..1): darkens corners near other voxels.
    float aoShade = 0.35 + 0.65 * vAO;
    outColor = vec4(albedo.rgb * vTint.rgb * shade * aoShade, albedo.a * vTint.a);
    if (outColor.a < 0.01) discard;
}
