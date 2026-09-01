#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kWaterFragWgsl = R"wgsl(
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
@group(0) @binding(6) var ssrTex: texture_2d<f32>;
@group(0) @binding(7) var mainSampler: sampler;
@group(0) @binding(15) var<uniform> params: Externals;
@group(0) @binding(16) var reflectionProbe0: texture_cube<f32>;
@group(0) @binding(17) var reflectionProbe1: texture_cube<f32>;
fn p(i: u32) -> f32 { return params.data[i / 4u][i % 4u]; }
fn hash(n: f32) -> f32 { return fract(sin(n) * 43758.5453123); }
fn hash2(i: i32) -> vec2f {
    return vec2f(hash(f32(i) * 7.31), hash(f32(i) * 13.17 + 1.0));
}
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
fn rippleRing(uv: vec2f, i: i32) -> f32 {
    let period = max(p(7u), 0.001);
    let local = p(0u) - floor(p(0u) / period) * period;
    let age = local - hash(f32(i) * 3.7) * period;
    if (age < 0.0 || age > period * 0.8) { return 0.0; }
    let center = mix(vec2f(0.5), hash2(i), vec2f(0.72));
    let x = (length(uv - center) - age * 0.22) / 0.10;
    return p(3u) * exp(-(x * x) * 0.9) * cos(x * 6.28318) * exp(-age * 1.6);
}
fn waterHeight(uv: vec2f) -> f32 {
    let edgeDist = min(uv, vec2f(1.0) - uv);
    let edge = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(p(4u), 0.0001), 0.0, 1.0);
    var w = edge * p(2u) *
        (sin((uv.x * p(8u) + p(0u) * p(1u)) * 6.2831853) +
         0.5 * sin((uv.y * p(8u) * 0.7 - p(0u) * p(1u) * 1.3) * 6.2831853));
    w += p(2u) * 0.10 *
         sin((uv.x * 31.0 + uv.y * 17.0 + p(0u) * p(1u) * 2.0) * 6.2831853);
    for (var i = 0; i < 8; i++) {
        if (f32(i) >= p(6u) + 0.5) { break; }
        w += rippleRing(uv, i);
    }
    return w;
}
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    let eps = 0.001;
    let grad = vec2f(waterHeight(in.uv + vec2f(eps, 0.0)) -
                     waterHeight(in.uv - vec2f(eps, 0.0)),
                     waterHeight(in.uv + vec2f(0.0, eps)) -
                     waterHeight(in.uv - vec2f(0.0, eps))) / (2.0 * eps);
    let n = normalize(vec3f(-grad.x, 1.0, -grad.y));
    let v = normalize(frame.cameraPos.xyz - in.worldPos);
    let reflected = reflect(-v, n);
    let ndv = max(dot(v, n), 0.0);
    let roughness = clamp(0.08 + length(grad) * 0.06, 0.04, 0.35);
    let maxLod = f32(max(i32(textureNumLevels(environment)) - 1, 0));
    let specMaxLod = select(maxLod, maxLod - 1.0, maxLod >= 2.0);
    let lod = roughness * specMaxLod;
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
    reflection *= vec3f(p(12u), p(13u), p(14u));
    let fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    let brdf = roughness * vec4f(-1.0, -0.0275, -0.572, 0.022) +
               vec4f(1.0, 0.0425, 1.04, -0.04);
    let a004 = min(brdf.x * brdf.x, exp2(-9.28 * ndv)) * brdf.x + brdf.y;
    let dfg = vec2f(-1.04, 1.04) * a004 + brdf.zw;
    let envWeight = max(0.02 * dfg.x + dfg.y, 0.0);
    var color = mix(vec3f(p(9u), p(10u), p(11u)), reflection,
                    clamp(max(fresnel, envWeight) * p(5u), 0.0, 1.0));
    let l = normalize(frame.lightDir.xyz);
    let h = normalize(v + l);
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
    color += frame.lightColor.rgb * spec * noL * p(15u);
    let edgeDist = min(in.uv, vec2f(1.0) - in.uv);
    let edge = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(p(4u), 0.0001), 0.0, 1.0);
    color = mix(color, vec3f(0.85, 0.93, 1.0), edge * 0.22);
    if (p(18u) > 0.5 && p(16u) > 1.0 && p(17u) > 1.0) {
        let suv = in.pos.xy / vec2f(p(16u), p(17u));
        let ssr = textureSample(ssrTex, mainSampler, suv);
        let ssrWeight = clamp(ssr.a * p(19u), 0.0, 1.0);
        color = ssr.rgb * p(19u) + color * (1.0 - ssrWeight);
    }
    return vec4f(color, 1.0);
}
)wgsl";

}  // namespace eve::graphics::shaders
