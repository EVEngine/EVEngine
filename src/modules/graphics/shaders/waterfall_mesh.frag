#version 450

#extension GL_GOOGLE_include_directive : enable
#include "tonemap.glsl"
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

struct Light3D { vec4 posRadius; vec4 color; };
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
    vec4 bindlessEnv;
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
    mat4 skinBones[128];
    vec4 reflectionProbeCenter[2];
    vec4 reflectionProbeExtent[2];
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform samplerCube env;
layout(set = 0, binding = 16) uniform samplerCube reflectionProbe0;
layout(set = 0, binding = 17) uniform samplerCube reflectionProbe1;

layout(push_constant) uniform Externals { float data[32]; } u;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

// Classic pseudo-random value noise.
float hash(float n) { return fract(sin(n) * 43758.5453123); }
float probeWeight(int index, vec3 worldPos) {
    vec3 edge = ubo.reflectionProbeExtent[index].xyz -
                abs(worldPos - ubo.reflectionProbeCenter[index].xyz);
    float inside = min(edge.x, min(edge.y, edge.z));
    if (inside <= 0.0 || ubo.reflectionProbeCenter[index].w <= 0.0) return 0.0;
    return clamp(inside / max(ubo.reflectionProbeExtent[index].w, 1e-4), 0.0, 1.0);
}
vec3 probeDirection(int index, vec3 direction, vec3 worldPos) {
    vec3 center = ubo.reflectionProbeCenter[index].xyz;
    vec3 extent = ubo.reflectionProbeExtent[index].xyz;
    vec3 safeDir = mix(vec3(1e-5), direction, greaterThan(abs(direction), vec3(1e-5)));
    vec3 exitT = max((center - extent - worldPos) / safeDir,
                     (center + extent - worldPos) / safeDir);
    float distanceToBox = min(exitT.x, min(exitT.y, exitT.z));
    return distanceToBox > 0.0
        ? normalize(worldPos + direction * distanceToBox - center)
        : normalize(direction);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    float a = hash(i.x + i.y * 57.0);
    float b = hash(i.x + 1.0 + i.y * 57.0);
    float c = hash(i.x + (i.y + 1.0) * 57.0);
    float d = hash(i.x + 1.0 + (i.y + 1.0) * 57.0);
    return mix(mix(a, b, s.x), mix(c, d, s.x), s.y);
}

// 2D fractal noise, driven by a downward-scrolling coordinate so the water
// always flows toward the pool at the bottom.
float flowNoise(vec2 uv, float t) {
    float speed = u.data[1];            // flowSpeed
    vec2 q = vec2(uv.x * 3.0, uv.y * 4.0 + t * speed);
    float n = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int o = 0; o < 4; ++o) {
        n += amp * noise(q * freq + float(o) * 13.7);
        freq *= 2.1;
        amp *= 0.5;
    }
    return n;
}

// Vertical flowing streaks: elongated in the fall direction by streakScale.
float streaks(vec2 uv, float t) {
    float scale = u.data[4];            // streakScale
    int n = int(u.data[3] + 0.5);       // streakCount
    float s = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= n) break;
        float x = fract(hash(float(i) * 3.1) + uv.x * 0.35) - 0.5;
        float phase = hash(float(i) * 9.7) * 10.0;
        float y = (uv.y - 0.5 + t * u.data[1] * 0.6) * scale + phase;
        float dx = exp(-abs(x) * 3.5);
        float dy = exp(-abs(y) * 1.5);
        s += dx * dy;
    }
    return s;
}

// Turbulent white-water crest intensity over the sheet (streaks + noise).
float cascade(vec2 uv, float t) {
    float turb = u.data[2];             // turbulence
    float n = flowNoise(uv, t);
    float st = streaks(uv, t);
    // Bands that slide downward, wider near the bottom like a churning fall.
    float band = 0.5 + 0.5 * sin((uv.y * 22.0 + t * u.data[1] * 3.0) * PI);
    float v = n * 0.6 + st * (0.5 + turb * 0.5) + band * 0.2;
    return clamp(v * turb, 0.0, 1.0);
}

void main() {
    float t = u.data[0];

    // Surface normal from the analytic displacement (finite differences on a
    // function of the unscrolled coordinate so streaks read as geometry bumps).
    vec2 p0 = vUV;
    float eps = 1e-3;
    vec2 pL = p0 - vec2(eps, 0.0);
    vec2 pR = p0 + vec2(eps, 0.0);
    vec2 pD = p0 - vec2(0.0, eps);
    vec2 pU = p0 + vec2(0.0, eps);
    float h  = cascade(p0, t);
    float hL = cascade(pL, t);
    float hR = cascade(pR, t);
    float hD = cascade(pD, t);
    float hU = cascade(pU, t);
    vec2 grad = vec2((hR - hL) / (2.0 * eps), (hU - hD) / (2.0 * eps));
    vec3 N = normalize(vec3(-grad.x, -grad.y, 1.0));   // plane faces +Z

    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 R = reflect(-V, N);

    // Microfacet sky reflection; turbulent slope broadens the lobe.
    float ndv = max(dot(V, N), 0.0);
    float roughness = clamp(0.14 + length(grad) * 0.08, 0.08, 0.48);
    float maxLod = float(max(textureQueryLevels(env) - 1, 0));
    float specMaxLod = maxLod >= 2.0 ? maxLod - 1.0 : maxLod;
    float lod = roughness * specMaxLod;
    float probeWeight0 = probeWeight(0, vWorldPos);
    float probeWeight1 = probeWeight(1, vWorldPos);
    float probeWeightSum = probeWeight0 + probeWeight1;
    if (probeWeightSum > 1.0) {
        probeWeight0 /= probeWeightSum;
        probeWeight1 /= probeWeightSum;
        probeWeightSum = 1.0;
    }
    vec3 refl = textureLod(env, R, lod).rgb * ubo.lightColor.w * (1.0 - probeWeightSum);
    if (probeWeight0 > 0.0) {
        float probeLod = float(max(textureQueryLevels(reflectionProbe0) - 1, 0));
        refl += textureLod(reflectionProbe0, probeDirection(0, R, vWorldPos),
                           roughness * max(probeLod - 1.0, 0.0)).rgb *
                ubo.reflectionProbeCenter[0].w * probeWeight0;
    }
    if (probeWeight1 > 0.0) {
        float probeLod = float(max(textureQueryLevels(reflectionProbe1) - 1, 0));
        refl += textureLod(reflectionProbe1, probeDirection(1, R, vWorldPos),
                           roughness * max(probeLod - 1.0, 0.0)).rgb *
                ubo.reflectionProbeCenter[1].w * probeWeight1;
    }
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    vec4 brdf = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) +
                vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(brdf.x * brdf.x, exp2(-9.28 * ndv)) * brdf.x + brdf.y;
    vec2 dfg = vec2(-1.04, 1.04) * a004 + brdf.zw;
    float envWeight = max(0.02 * dfg.x + dfg.y, 0.0);

    vec3 waterCol = vec3(u.data[10], u.data[11], u.data[12]);
    float reflectAmt = clamp(max(fresnel, envWeight) * u.data[8], 0.0, 1.0);
    vec3 color = mix(waterCol, refl, reflectAmt);

    // GGX sun glint, sharing the turbulent surface roughness.
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float alpha = max(roughness * roughness, 0.002);
    float alpha2 = alpha * alpha;
    float denom = NoH * NoH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / max(3.14159265 * denom * denom, 1e-4);
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float Gv = ndv / max(ndv * (1.0 - k) + k, 1e-4);
    float Gl = NoL / max(NoL * (1.0 - k) + k, 1e-4);
    float Fsun = 0.02 + 0.98 * pow(1.0 - VoH, 5.0);
    float spec = D * Gv * Gl * Fsun / max(4.0 * ndv * max(NoL, 0.001), 1e-3);
    color += ubo.lightColor.rgb * spec * NoL * u.data[9];

    // White-water foam: crests in the body plus foam bands at the top lip and
    // the splash pool at the bottom.
    float foam = cascade(p0, t) * u.data[7];
    float top = u.data[5];                              // topFoam
    float bottom = u.data[6];                           // bottomFoam
    float topBand = clamp((p0.y - (1.0 - top)) / max(top, 1e-4), 0.0, 1.0);
    float bottomBand = clamp((bottom - p0.y) / max(bottom, 1e-4), 0.0, 1.0);
    float edge = clamp(topBand + bottomBand, 0.0, 1.0);
    float foamy = clamp(foam + edge * 0.9, 0.0, 1.0);
    vec3 foamCol = vec3(0.90, 0.95, 1.0);
    color = mix(color, foamCol, foamy);

    // Fade to slightly transparent at the very bottom so it melts into the pool.
    outColor = vec4(color, 1.0 - bottomBand * 0.5);
}
