#version 450

// Continuous sphere terrain for examples/pcg-solar-planet.
// Inspired by SebLague Solar-System Episode 02 (height + slope colouring, triplanar noise).
// Vertex UV.x carries normalized elevation; UV.y carries a biome hash in [0,1].
// Tint.r is the ocean-level elevation in the same normalized space.

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
layout(location=0) out vec4 outColor;

const float PI = 3.14159265359;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x),
               mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += noise(p) * a; p = mat2(1.7, -1.3, 1.3, 1.7) * p; a *= 0.5; }
    return s;
}

// Cheap procedural triplanar sample (no texture): blend 2D fbm on the dominant planes.
float triplanarNoise(vec3 pos, vec3 n, float scale) {
    vec3 blending = abs(n);
    blending = max(blending, vec3(0.0001));
    blending /= (blending.x + blending.y + blending.z);
    float xy = fbm(pos.xy * scale);
    float yz = fbm(pos.yz * scale);
    float zx = fbm(pos.zx * scale);
    return xy * blending.z + yz * blending.x + zx * blending.y;
}

vec3 hostTonemapInverse(vec3 srgb) {
    srgb = clamp(srgb, vec3(0.0), vec3(0.95));
    return (0.139 * srgb) / max(vec3(1.0) - srgb, vec3(0.001));
}

void main() {
    float time = ubo.cloud.z;
    vec3 radial = normalize(vWorldPos);
    vec3 N = normalize(vNormal);

    float height01 = clamp(vUV.x, 0.0, 1.0);
    float biome = clamp(vUV.y, 0.0, 1.0);
    float oceanLevel = clamp(vTint.r, 0.0, 1.0);
    float aboveShore = clamp((height01 - oceanLevel) / max(1.0 - oceanLevel, 0.001), 0.0, 1.0);

    // Steepness: 0 flat (normal aligns with radius), 1 cliff.
    float steepness = 1.0 - clamp(dot(N, radial), 0.0, 1.0);
    steepness = clamp(steepness / 0.65, 0.0, 1.0);

    float grain = triplanarNoise(vWorldPos, N, 3.2);
    float grainFine = triplanarNoise(vWorldPos, N, 9.0);

    vec3 shoreLow  = vec3(0.62, 0.56, 0.34);
    vec3 shoreHigh = vec3(0.82, 0.74, 0.48);
    vec3 flatLowA  = vec3(0.20, 0.38, 0.12);
    vec3 flatHighA = vec3(0.40, 0.58, 0.20);
    vec3 flatLowB  = vec3(0.34, 0.30, 0.16);
    vec3 flatHighB = vec3(0.52, 0.44, 0.24);
    vec3 steepLow  = vec3(0.28, 0.27, 0.26);
    vec3 steepHigh = vec3(0.55, 0.52, 0.48);
    vec3 snowCol   = vec3(0.90, 0.94, 0.98);

    float flatT = smoothstep(0.0, 0.55, aboveShore);
    vec3 flatA = mix(flatLowA, flatHighA, flatT);
    vec3 flatB = mix(flatLowB, flatHighB, flatT);
    vec3 flatCol = mix(flatA, flatB, biome);
    flatCol = mix(flatCol, (flatA + flatB) * 0.5, grainFine * 0.25);

    float shoreW = 1.0 - smoothstep(0.0, 0.08, aboveShore);
    vec3 shoreCol = mix(shoreLow, shoreHigh, clamp(aboveShore / 0.08, 0.0, 1.0));
    shoreCol = mix(shoreCol, (shoreLow + shoreHigh) * 0.5, grain * 0.35);
    flatCol = mix(flatCol, shoreCol, shoreW);

    vec3 steepCol = mix(steepLow, steepHigh, aboveShore + (grain - 0.5) * 0.2);
    float flatStrength = 1.0 - smoothstep(0.42, 0.62, steepness);
    flatStrength *= 1.0 - smoothstep(0.55, 0.85, aboveShore);

    vec3 albedo = mix(steepCol, flatCol, flatStrength);
    float snowW = smoothstep(0.72, 0.90, abs(radial.y) + (grain - 0.5) * 0.05) * smoothstep(0.35, 0.7, aboveShore);
    albedo = mix(albedo, snowCol * mix(1.0, 1.15, aboveShore), snowW);
    albedo *= mix(0.90, 1.08, grain);

    float roughness = mix(0.92, 0.55, snowW);
    roughness = mix(roughness, 0.70, steepness * (1.0 - snowW));

    float detail = triplanarNoise(vWorldPos, N, 14.0);
    N = normalize(N + vec3(dFdx(detail), 0.0, dFdy(detail)) * 0.18);

    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.001);
    float ndh = max(dot(N, H), 0.0);
    float alpha = max(0.03, roughness * roughness), a2 = alpha * alpha;
    float D = a2 / max(PI * pow(ndh * ndh * (a2 - 1.0) + 1.0, 2.0), 0.001);
    float k = pow(roughness + 1.0, 2.0) / 8.0;
    float G = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
    vec3 F0 = vec3(0.04);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 spec = D * G * F / max(4.0 * ndl * ndv, 0.001);
    vec3 direct = ((1.0 - F) * albedo / PI + spec) * ndl * ubo.lightColor.rgb * 1.5;

    float up = dot(N, radial);
    float sky = max(0.0, up) * 0.16 + 0.06;
    vec3 color = direct + albedo * (ubo.ambient.rgb * 0.55 + vec3(0.10, 0.12, 0.15) * sky);
    color *= mix(0.72, 1.0, clamp(up * 1.35 + 0.20, 0.0, 1.0));

    float rim = pow(1.0 - clamp(ndv, 0.0, 1.0), 3.0);
    color += vec3(0.10, 0.22, 0.44) * rim * 0.35;

    color = color / (1.0 + color * 1.05);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(hostTonemapInverse(color), 1.0);
}
