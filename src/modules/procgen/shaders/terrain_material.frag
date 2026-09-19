#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(set=0,binding=1) uniform sampler2D splatSampler;
layout(set=0,binding=22) uniform sampler2D terrainAlbedoAtlas;
layout(set=0,binding=23) uniform sampler2D terrainNormalAtlas;
layout(set=0,binding=24) uniform sampler2D terrainMaskAtlas;
layout(set=0,binding=25) uniform sampler2D terrainHoles;
layout(push_constant) uniform TerrainParams { float data[32]; } terrain;
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadow;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;
layout(location=0) out vec4 outColor;

float hash21(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p)));
    uint n = q.x * 1597334677u ^ q.y * 3812015801u;
    n = (n ^ (n >> 15u)) * 2246822519u;
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float terrainShadow(vec3 worldPos, float viewDepth) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;
    int cascade = viewDepth < shadow.splits.x ? 0 : (viewDepth < shadow.splits.y ? 1 : 2);
    vec4 clip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 ||
        ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;
    float compareBias = cascade == 0 ? shadow.cascadeBias.x :
                        (cascade == 1 ? shadow.cascadeBias.y : shadow.cascadeBias.z);
    if (compareBias <= 1e-8) compareBias = shadow.bias.x;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
        visibility += texture(shadowMap,
            vec4(uv + vec2(x, y) * texel * 0.65, float(cascade), ndc.z - compareBias));
    return visibility / 9.0;
}
float packedParameter(int parameter, int layer) {
    uvec4 bytes = uvec4(texelFetch(terrainHoles, ivec2(parameter, layer), 0) * 255.0 + 0.5);
    uint bits = bytes.r | (bytes.g << 8u) | (bytes.b << 16u) | (bytes.a << 24u);
    return uintBitsToFloat(bits);
}
float packedWeight(int layer) {
    int group = layer >> 2;
    vec2 cell = vec2(float(group % 3), float(group / 3));
    vec4 control = max(texture(splatSampler, (vUV + cell) / vec2(3.0, 2.0)), vec4(0.0));
    return control[layer & 3];
}
void main() {
    bool packed = terrain.data[28] > 1.5;
    vec4 weights = max(texture(splatSampler, packed ? vUV / vec2(3.0, 2.0) : vUV), vec4(0.0));
    if (!packed) weights /= max(dot(weights, vec4(1.0)), 1e-5);
    const vec3 sand = vec3(0.34, 0.235, 0.115);
    const vec3 vegetation = vec3(0.075, 0.255, 0.055);
    const vec3 rock = vec3(0.285, 0.275, 0.255);
    const vec3 snow = vec3(0.78, 0.82, 0.86);
    vec3 albedo = sand * weights.r + vegetation * weights.g +
                  rock * weights.b + snow * weights.a;
    vec3 sampledNormal = vec3(0.0, 0.0, 1.0);
    float sampledMetallic = 0.0;
    float sampledSmoothness = 1.0 - dot(weights, vec4(0.82, 0.94, 0.68, 0.52));
    float snowWeight = weights.a;
    if (packed) {
        vec3 albedoSum = vec3(0.0);
        vec3 normalSum = vec3(0.0);
        float metallicSum = 0.0;
        float smoothnessSum = 0.0;
        float totalWeight = 0.0;
        float layerThreeWeight = 0.0;
        int layerCount = clamp(int(terrain.data[30] + 0.5), 1, 16);
        for (int layer = 0; layer < layerCount; ++layer) totalWeight += packedWeight(layer);
        for (int layer = 0; layer < layerCount; ++layer) {
            float w = totalWeight > 1e-5 ? packedWeight(layer) / totalWeight : (layer == 0 ? 1.0 : 0.0);
            if (layer == 3) layerThreeWeight = w;
            vec2 layerUV = vWorldPos.xz * vec2(packedParameter(0, layer), packedParameter(1, layer)) +
                           vec2(packedParameter(2, layer), packedParameter(3, layer));
            vec2 atlasUV = (fract(layerUV) + vec2(float(layer & 3), float(layer >> 2))) * 0.25;
            vec4 layerAlbedo = texture(terrainAlbedoAtlas, atlasUV);
            vec3 layerNormal = texture(terrainNormalAtlas, atlasUV).xyz * 2.0 - 1.0;
            layerNormal.xy *= packedParameter(5, layer);
            vec4 layerMask = texture(terrainMaskAtlas, atlasUV);
            albedoSum += layerAlbedo.rgb * w;
            normalSum += normalize(layerNormal) * w;
            metallicSum += layerMask.r * packedParameter(4, layer) * w;
            smoothnessSum += layerMask.a * packedParameter(6, layer) * w;
        }
        albedo = albedoSum;
        sampledNormal = normalize(normalSum);
        sampledMetallic = clamp(metallicSum, 0.0, 1.0);
        sampledSmoothness = clamp(smoothnessSum, 0.0, 1.0);
        snowWeight = layerThreeWeight;
        if (terrain.data[29] > 0.5 &&
            texture(splatSampler, (vUV + vec2(1.0, 1.0)) / vec2(3.0, 2.0)).r < 0.5) discard;
    } else if (terrain.data[28] > 0.5) {
        vec3 albedoSum = vec3(0.0);
        vec3 normalSum = vec3(0.0);
        float metallicSum = 0.0;
        float smoothnessSum = 0.0;
        for (int layer = 0; layer < 4; ++layer) {
            int st = layer * 4;
            vec2 layerUV = vWorldPos.xz * vec2(terrain.data[st], terrain.data[st + 1]) +
                           vec2(terrain.data[st + 2], terrain.data[st + 3]);
            vec2 atlasUV = (fract(layerUV) + vec2(float(layer & 1), float(layer >> 1))) * 0.5;
            vec4 layerAlbedo = texture(terrainAlbedoAtlas, atlasUV);
            vec4 layerNormal = texture(terrainNormalAtlas, atlasUV);
            vec4 layerMask = texture(terrainMaskAtlas, atlasUV);
            float w = weights[layer];
            albedoSum += layerAlbedo.rgb * w;
            vec3 tangentNormal = layerNormal.xyz * 2.0 - 1.0;
            tangentNormal.xy *= terrain.data[20 + layer];
            normalSum += normalize(tangentNormal) * w;
            metallicSum += layerMask.r * terrain.data[16 + layer] * w;
            smoothnessSum += layerMask.a * terrain.data[24 + layer] * w;
        }
        albedo = albedoSum;
        sampledNormal = normalize(normalSum);
        sampledMetallic = clamp(metallicSum, 0.0, 1.0);
        sampledSmoothness = clamp(smoothnessSum, 0.0, 1.0);
        if (terrain.data[29] > 0.5 && texture(terrainHoles, vUV).r < 0.5) discard;
    }
    float macro = mix(0.88, 1.12, valueNoise(vWorldPos.xz * 0.55));
    float fine = mix(0.94, 1.06, valueNoise(vWorldPos.xz * 5.0 + vec2(17.0, 31.0)));
    albedo *= macro * fine * vTint.rgb;
    float roughness = 1.0 - sampledSmoothness;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0) N = -N;
    vec2 detailP = vWorldPos.xz * 4.0;
    float detail0 = valueNoise(detailP);
    float detailX = valueNoise(detailP + vec2(0.08, 0.0));
    float detailZ = valueNoise(detailP + vec2(0.0, 0.08));
    N = normalize(N + vec3(detail0 - detailX, 0.0, detail0 - detailZ) *
                  mix(0.16, 0.045, snowWeight));
    if (terrain.data[28] > 0.5) {
        vec3 dpdx = dFdx(vWorldPos);
        vec3 dpdy = dFdy(vWorldPos);
        vec2 duvdx = dFdx(vUV);
        vec2 duvdy = dFdy(vUV);
        float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
        if (abs(determinant) > 1e-7) {
            vec3 T = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / determinant);
            vec3 B = normalize(cross(N, T));
            N = normalize(mat3(T, B, N) * sampledNormal);
        }
    }
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float wrap = ndl * 0.88 + 0.12;
    float specPower = mix(96.0, 5.0, roughness);
    float spec = pow(max(dot(N, H), 0.0), specPower) *
                 mix(mix(0.22, 0.035, roughness), 1.0, sampledMetallic);
    vec3 sky = ubo.ambient.rgb * mix(vec3(0.72, 0.64, 0.55), vec3(1.05), N.y * 0.5 + 0.5);
    float viewDepth = max(-vViewPos.z, 0.0);
    float visibility = terrainShadow(vWorldPos, viewDepth);
    vec3 color = albedo * (sky + ubo.lightColor.rgb * wrap * visibility) +
                 ubo.lightColor.rgb * spec * visibility;
    color = color / (color + vec3(0.72));
    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float linearDepth = clamp((viewDepth - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outColor = vec4(color, linearDepth);
}
