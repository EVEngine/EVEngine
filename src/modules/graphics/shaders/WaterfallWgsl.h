#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kWaterfallFragWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame {
    mvp: mat4x4f, model: mat4x4f, lightDir: vec4f, lightColor: vec4f,
    tint: vec4f, cameraPos: vec4f, ambient: vec4f,
    lights: array<Light3D, 8>, texBomb: vec4f, parallax: vec4f,
    surface: vec4f, view: mat4x4f, clipInfo: vec4f, cloud: vec4f, cloudWind: vec4f,
    envProbeCenter: vec4f, envProbeExtent: vec4f, skinInfo: vec4f,
    skinBones: array<mat4x4f, 128>, reflectionProbeCenter: array<vec4f, 2>,
    reflectionProbeExtent: array<vec4f, 2>,
};
struct FSIn {
    @builtin(position) pos: vec4f,
    @location(0) normal: vec3f, @location(1) uv: vec2f, @location(2) tint: vec4f,
    @location(3) worldPos: vec3f, @location(4) cameraPos: vec3f,
    @location(5) viewPos: vec3f,
};
struct Externals { data: array<vec4f, 8> };
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(3) var environment: texture_cube<f32>;
@group(0) @binding(7) var mainSampler: sampler;
@group(0) @binding(15) var<uniform> params: Externals;
@group(0) @binding(16) var reflectionProbe0: texture_cube<f32>;
@group(0) @binding(17) var reflectionProbe1: texture_cube<f32>;
fn p(i: u32) -> f32 { return params.data[i / 4u][i % 4u]; }
fn hash(n: f32) -> f32 { return fract(sin(n) * 43758.5453123); }
fn probeWeight(index: u32, worldPos: vec3f) -> f32 {
    let edge = frame.reflectionProbeExtent[index].xyz -
               abs(worldPos - frame.reflectionProbeCenter[index].xyz);
    let inside = min(edge.x, min(edge.y, edge.z));
    return select(0.0,
        clamp(inside / max(frame.reflectionProbeExtent[index].w, 0.0001), 0.0, 1.0),
        inside > 0.0 && frame.reflectionProbeCenter[index].w > 0.0);
}
fn probeDirection(index: u32, direction: vec3f, worldPos: vec3f) -> vec3f {
    let center = frame.reflectionProbeCenter[index].xyz;
    let extent = frame.reflectionProbeExtent[index].xyz;
    let safeDir = select(vec3f(0.00001), direction, abs(direction) > vec3f(0.00001));
    let exitT = max((center - extent - worldPos) / safeDir,
                    (center + extent - worldPos) / safeDir);
    let distanceToBox = min(exitT.x, min(exitT.y, exitT.z));
    return select(normalize(direction),
        normalize(worldPos + direction * distanceToBox - center), distanceToBox > 0.0);
}
fn noise(q: vec2f) -> f32 {
    let i = floor(q); let f = fract(q); let s = f * f * (vec2f(3.0) - 2.0 * f);
    let a = hash(i.x + i.y * 57.0); let b = hash(i.x + 1.0 + i.y * 57.0);
    let c = hash(i.x + (i.y + 1.0) * 57.0);
    let d = hash(i.x + 1.0 + (i.y + 1.0) * 57.0);
    return mix(mix(a, b, s.x), mix(c, d, s.x), s.y);
}
fn flowNoise(uv: vec2f, time: f32) -> f32 {
    let q = vec2f(uv.x * 3.0, uv.y * 4.0 + time * p(1u));
    var value = 0.0; var amplitude = 0.5; var frequency = 1.0;
    for (var octave = 0; octave < 4; octave++) {
        value += amplitude * noise(q * frequency + vec2f(f32(octave) * 13.7));
        frequency *= 2.1; amplitude *= 0.5;
    }
    return value;
}
fn streaks(uv: vec2f, time: f32) -> f32 {
    var value = 0.0;
    for (var i = 0; i < 6; i++) {
        if (f32(i) >= p(3u) + 0.5) { break; }
        let x = fract(hash(f32(i) * 3.1) + uv.x * 0.35) - 0.5;
        let y = (uv.y - 0.5 + time * p(1u) * 0.6) * p(4u) + hash(f32(i) * 9.7) * 10.0;
        value += exp(-abs(x) * 3.5) * exp(-abs(y) * 1.5);
    }
    return value;
}
fn cascade(uv: vec2f, time: f32) -> f32 {
    let band = 0.5 + 0.5 * sin((uv.y * 22.0 + time * p(1u) * 3.0) * 3.14159265);
    return clamp((flowNoise(uv, time) * 0.6 + streaks(uv, time) *
                  (0.5 + p(2u) * 0.5) + band * 0.2) * p(2u), 0.0, 1.0);
}
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    let eps = 0.001; let time = p(0u);
    let grad = vec2f(cascade(in.uv + vec2f(eps, 0.0), time) -
                     cascade(in.uv - vec2f(eps, 0.0), time),
                     cascade(in.uv + vec2f(0.0, eps), time) -
                     cascade(in.uv - vec2f(0.0, eps), time)) / (2.0 * eps);
    let n = normalize(vec3f(-grad.x, -grad.y, 1.0));
    let v = normalize(frame.cameraPos.xyz - in.worldPos);
    let ndv = max(dot(v, n), 0.0);
    let roughness = clamp(0.14 + length(grad) * 0.08, 0.08, 0.48);
    let maxLod = f32(max(i32(textureNumLevels(environment)) - 1, 0));
    let specMaxLod = select(maxLod, maxLod - 1.0, maxLod >= 2.0);
    let lod = roughness * specMaxLod;
    let reflected = reflect(-v, n);
    var probeWeight0 = probeWeight(0u, in.worldPos);
    var probeWeight1 = probeWeight(1u, in.worldPos);
    var probeWeightSum = probeWeight0 + probeWeight1;
    if (probeWeightSum > 1.0) {
        probeWeight0 /= probeWeightSum;
        probeWeight1 /= probeWeightSum;
        probeWeightSum = 1.0;
    }
    var reflection = textureSampleLevel(environment, mainSampler, reflected, lod).rgb *
                     frame.lightColor.w * (1.0 - probeWeightSum);
    // @lifetime Shader math below does not declare host pointers.
    if (probeWeight0 > 0.0) {
        let probeLod = f32(max(i32(textureNumLevels(reflectionProbe0)) - 1, 0));
        reflection += textureSampleLevel(reflectionProbe0, mainSampler,
            probeDirection(0u, reflected, in.worldPos),
            roughness * max(probeLod - 1.0, 0.0)).rgb *
            frame.reflectionProbeCenter[0].w * probeWeight0;
    }
    if (probeWeight1 > 0.0) {
        let probeLod = f32(max(i32(textureNumLevels(reflectionProbe1)) - 1, 0));
        reflection += textureSampleLevel(reflectionProbe1, mainSampler,
            probeDirection(1u, reflected, in.worldPos),
            roughness * max(probeLod - 1.0, 0.0)).rgb *
            frame.reflectionProbeCenter[1].w * probeWeight1;
    }
    let fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    let brdf = roughness * vec4f(-1.0, -0.0275, -0.572, 0.022) +
               vec4f(1.0, 0.0425, 1.04, -0.04);
    let a004 = min(brdf.x * brdf.x, exp2(-9.28 * ndv)) * brdf.x + brdf.y;
    let dfg = vec2f(-1.04, 1.04) * a004 + brdf.zw;
    let envWeight = max(0.02 * dfg.x + dfg.y, 0.0);
    var color = mix(vec3f(p(10u), p(11u), p(12u)), reflection,
                    clamp(max(fresnel, envWeight) * p(8u), 0.0, 1.0));
    let h = normalize(v + normalize(frame.lightDir.xyz));
    let l = normalize(frame.lightDir.xyz);
    let noL = max(dot(n, l), 0.0);
    let noH = max(dot(n, h), 0.0);
    let voH = max(dot(v, h), 0.0);
    let alpha = max(roughness * roughness, 0.002);
    let alpha2 = alpha * alpha;
    let denom = noH * noH * (alpha2 - 1.0) + 1.0;
    let d = alpha2 / max(3.14159265 * denom * denom, 0.0001);
    let k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    let gv = ndv / max(ndv * (1.0 - k) + k, 0.0001);
    let gl = noL / max(noL * (1.0 - k) + k, 0.0001);
    let fsun = 0.02 + 0.98 * pow(1.0 - voH, 5.0);
    let spec = d * gv * gl * fsun / max(4.0 * ndv * max(noL, 0.001), 0.001);
    color += frame.lightColor.rgb * spec * noL * p(9u);
    let topBand = clamp((in.uv.y - (1.0 - p(5u))) / max(p(5u), 0.0001), 0.0, 1.0);
    let bottomBand = clamp((p(6u) - in.uv.y) / max(p(6u), 0.0001), 0.0, 1.0);
    let foam = clamp(cascade(in.uv, time) * p(7u) +
                     clamp(topBand + bottomBand, 0.0, 1.0) * 0.9, 0.0, 1.0);
    color = mix(color, vec3f(0.90, 0.95, 1.0), foam);
    return vec4f(color, 1.0 - bottomBand * 0.5);
}
)wgsl";

}  // namespace eve::graphics::shaders
