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
layout(set = 0, binding = 6) uniform sampler2D ssrTex;  // screen-space reflection (optional)
layout(set = 0, binding = 16) uniform samplerCube reflectionProbe0;
layout(set = 0, binding = 17) uniform samplerCube reflectionProbe1;

layout(push_constant) uniform Externals { float data[32]; } u;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

float hash(float n) { return fract(sin(n) * 43758.5453123); }
vec2  hash2(int i) { return vec2(hash(float(i) * 7.31), hash(float(i) * 13.17 + 1.0)); }

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

// Expanding damped-wavelet ripple from a periodic drop (smooth, water-like).
float rippleRing(vec2 uv, int i) {
    float period = max(u.data[7], 1e-3);        // rippleInterval
    float local = mod(u.data[0], period);
    float startPhase = hash(float(i) * 3.7) * period;
    float age = local - startPhase;
    if (age < 0.0) return 0.0;
    float life = period * 0.8;
    if (age > life) return 0.0;
    vec2 center = hash2(i);
    center = mix(vec2(0.5), center, 0.72);      // keep drops near the middle
    float r = length(uv - center);
    float radius = age * 0.22;                  // expanding ring
    float wavelength = 0.10;                    // spacing between crests
    float x = (r - radius) / wavelength;
    // Soft damped wavelet: smooth gaussian envelope, a crest + trough pair.
    float envelope = exp(-(x * x) * 0.9);
    float wave = cos(x * 6.28318);
    float fade = exp(-age * 1.6);               // fade out as it grows
    return u.data[3] * envelope * wave * fade;  // rippleAmp
}

// Water height displacement over UV.
float waterHeight(vec2 uv) {
    float t = u.data[0];
    float ws = u.data[8];
    vec2 edgeDist = min(uv, vec2(1.0) - uv);
    float edgeFactor = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(u.data[4], 1e-4), 0.0, 1.0);
    float w = 0.0;
    // Shore-edge waves, strongest at the border and fading inward.
    w += edgeFactor * u.data[2] * (sin((uv.x * ws + t * u.data[1]) * PI * 2.0) +
                                   0.5 * sin((uv.y * ws * 0.7 - t * u.data[1] * 1.3) * PI * 2.0));
    // Fine detail everywhere.
    w += u.data[2] * 0.10 * sin((uv.x * 31.0 + uv.y * 17.0 + t * u.data[1] * 2.0) * PI * 2.0);
    // Occasional middle drop ripples.
    int n = int(u.data[6] + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= n) break;
        w += rippleRing(uv, i);
    }
    return w;
}

void main() {
    // Surface normal from the analytic displacement (finite differences).
    float eps = 1e-3;
    float hL = waterHeight(vUV - vec2(eps, 0.0));
    float hR = waterHeight(vUV + vec2(eps, 0.0));
    float hD = waterHeight(vUV - vec2(0.0, eps));
    float hU = waterHeight(vUV + vec2(0.0, eps));
    vec2 grad = vec2((hR - hL) / (2.0 * eps), (hU - hD) / (2.0 * eps));
    vec3 N = normalize(vec3(-grad.x, 1.0, -grad.y));

    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 R = reflect(-V, N);

    // Microfacet sky reflection; wave slope drives surface roughness.
    float ndv = max(dot(V, N), 0.0);
    float roughness = clamp(0.08 + length(grad) * 0.06, 0.04, 0.35);
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
    refl *= vec3(u.data[12], u.data[13], u.data[14]);
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    vec4 brdf = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) +
                vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(brdf.x * brdf.x, exp2(-9.28 * ndv)) * brdf.x + brdf.y;
    vec2 dfg = vec2(-1.04, 1.04) * a004 + brdf.zw;
    float envWeight = max(0.02 * dfg.x + dfg.y, 0.0);

    vec3 waterCol = vec3(u.data[9], u.data[10], u.data[11]);
    float reflectAmt = clamp(max(fresnel, envWeight) * u.data[5], 0.0, 1.0);
    vec3 color = mix(waterCol, refl, reflectAmt);

    // GGX sun glint, sharing the same wave-driven roughness as environment IBL.
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
    color += ubo.lightColor.rgb * spec * NoL * u.data[15];

    // Soft foam where waves meet the edge.
    vec2 edgeDist = min(vUV, vec2(1.0) - vUV);
    float edgeFactor = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(u.data[4], 1e-4), 0.0, 1.0);
    color = mix(color, vec3(0.85, 0.93, 1.0), edgeFactor * 0.22);
    // Optional screen-space reflection overlay (bound via the height slot,
    // binding 6). Sample the SSR pass result at this fragment's screen UV and
    // blend it over the env reflection where SSR found a hit (ssr.a > 0).
    if (u.data[18] > 0.5 && u.data[16] > 1.0 && u.data[17] > 1.0) {
        vec2 sUV = vec2(gl_FragCoord.x / u.data[16], gl_FragCoord.y / u.data[17]);
        vec4 ssr = texture(ssrTex, sUV);
        float ssrWeight = clamp(ssr.a * u.data[19], 0.0, 1.0);
        color = ssr.rgb * u.data[19] + color * (1.0 - ssrWeight);
    }

    outColor = vec4(color, 1.0);
}
