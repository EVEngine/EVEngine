#version 450
// Height + noise coverage: human tint -> glossy black goo with emissive edge.
// Push constants (declareFloat order):
//   0 coverage  1 edgeWidth  2 noiseScale  3 time
//   4 gooR 5 gooG 6 gooB  7 edgeR 8 edgeG 9 edgeB  10 edgeGlow  11 gloss

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 0) out vec4 outColor;

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
    vec4 cloud;
    vec4 cloudWind;
} ubo;

layout(push_constant) uniform Externals {
    float data[32];
} pc;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
               f.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; ++i) {
        sum += amp * noise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return sum;
}

void main() {
    float coverage = clamp(pc.data[0], 0.0, 1.0);
    float edgeWidth = max(pc.data[1], 1e-3);
    float noiseScale = max(pc.data[2], 1e-3);
    float time = pc.data[3];
    vec3 gooColor = vec3(pc.data[4], pc.data[5], pc.data[6]);
    vec3 edgeColor = vec3(pc.data[7], pc.data[8], pc.data[9]);
    float edgeGlow = max(pc.data[10], 0.0);
    float gloss = clamp(pc.data[11], 0.0, 1.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    if (dot(N, V) < 0.0)
        N = -N;

    // Bottom-to-top organic front.
    float height01 = clamp((vWorldPos.y + 0.05) / 1.85, 0.0, 1.0);
    float n = fbm(vWorldPos.xz * noiseScale + vec2(time * 0.35, time * 0.22));
    n = mix(n, fbm(vUV * noiseScale * 3.0 + time * 0.15), 0.35);
    float field = height01 + (n - 0.5) * 0.28;
    float frontier = coverage * 1.18 - 0.06;
    // 0 = human, 1 = goo. Coverage grows upward from the feet.
    float mask = 1.0 - smoothstep(frontier - edgeWidth, frontier + edgeWidth * 0.35, field);
    float edge = (1.0 - smoothstep(0.0, edgeWidth * 2.2, abs(field - frontier)))
               * step(0.02, coverage) * step(coverage, 0.98);

    float ndotl = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), mix(16.0, 96.0, gloss));
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    vec3 human = vTint.rgb * (0.22 + 0.78 * ndotl);
    human += ubo.ambient.rgb * vTint.rgb * 0.55;

    vec3 goo = gooColor * (0.08 + 0.55 * ndotl);
    goo += ubo.lightColor.rgb * spec * gloss * 1.35;
    goo += fresnel * vec3(0.18, 0.05, 0.28) * (0.45 + 0.55 * gloss);
    goo += ubo.ambient.rgb * gooColor * 0.15;

    float veins = smoothstep(0.62, 0.85, fbm(vWorldPos.xy * 7.0 + time * 0.8));
    goo = mix(goo, goo * 1.35 + vec3(0.12, 0.02, 0.18), veins * mask * 0.45);

    vec3 color = mix(human, goo, mask);
    color += edgeColor * edge * edge * edgeGlow;
    outColor = vec4(color, vTint.a);
}
