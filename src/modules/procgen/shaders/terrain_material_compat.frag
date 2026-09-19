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
void main() {
    vec4 weights = max(texture(splatSampler, vUV), vec4(0.0));
    weights /= max(dot(weights, vec4(1.0)), 1e-5);
    const vec3 sand = vec3(0.34, 0.235, 0.115);
    const vec3 vegetation = vec3(0.075, 0.255, 0.055);
    const vec3 rock = vec3(0.285, 0.275, 0.255);
    const vec3 snow = vec3(0.78, 0.82, 0.86);
    vec3 albedo = sand * weights.r + vegetation * weights.g +
                  rock * weights.b + snow * weights.a;
    float macro = mix(0.88, 1.12, valueNoise(vWorldPos.xz * 0.55));
    float fine = mix(0.94, 1.06, valueNoise(vWorldPos.xz * 5.0 + vec2(17.0, 31.0)));
    albedo *= macro * fine * vTint.rgb;
    float roughness = dot(weights, vec4(0.82, 0.94, 0.68, 0.52));
    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0) N = -N;
    vec2 detailP = vWorldPos.xz * 4.0;
    float detail0 = valueNoise(detailP);
    float detailX = valueNoise(detailP + vec2(0.08, 0.0));
    float detailZ = valueNoise(detailP + vec2(0.0, 0.08));
    N = normalize(N + vec3(detail0 - detailX, 0.0, detail0 - detailZ) *
                  mix(0.16, 0.045, weights.a));
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float wrap = ndl * 0.88 + 0.12;
    float specPower = mix(96.0, 5.0, roughness);
    float spec = pow(max(dot(N, H), 0.0), specPower) * mix(0.22, 0.035, roughness);
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

