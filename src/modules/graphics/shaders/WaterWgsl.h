#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kWaterFragWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame {
    mvp: mat4x4f, model: mat4x4f, lightDir: vec4f, lightColor: vec4f,
    tint: vec4f, cameraPos: vec4f, ambient: vec4f,
    lights: array<Light3D, 8>, texBomb: vec4f, parallax: vec4f,
    surface: vec4f, view: mat4x4f, clipInfo: vec4f, cloud: vec4f, cloudWind: vec4f,
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
fn p(i: u32) -> f32 { return params.data[i / 4u][i % 4u]; }
fn hash(n: f32) -> f32 { return fract(sin(n) * 43758.5453123); }
fn hash2(i: i32) -> vec2f {
    return vec2f(hash(f32(i) * 7.31), hash(f32(i) * 13.17 + 1.0));
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
    let maxLod = f32(max(i32(textureNumLevels(environment)) - 1, 0));
    let lod = min(1.0 + (1.0 - ndv) * 4.0, maxLod);
    let reflection = textureSampleLevel(environment, mainSampler, reflected, lod).rgb *
                     vec3f(p(12u), p(13u), p(14u));
    let fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    var color = mix(vec3f(p(9u), p(10u), p(11u)), reflection,
                    clamp(fresnel * p(5u) + 0.30, 0.0, 1.0));
    let l = normalize(frame.lightDir.xyz);
    let h = normalize(v + l);
    color += frame.lightColor.rgb * pow(max(dot(n, h), 0.0), 96.0) * p(15u);
    let edgeDist = min(in.uv, vec2f(1.0) - in.uv);
    let edge = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(p(4u), 0.0001), 0.0, 1.0);
    color = mix(color, vec3f(0.85, 0.93, 1.0), edge * 0.22);
    if (p(18u) > 0.5 && p(16u) > 1.0 && p(17u) > 1.0) {
        let suv = in.pos.xy / vec2f(p(16u), p(17u));
        let ssr = textureSample(ssrTex, mainSampler, suv);
        color = mix(color, ssr.rgb, clamp(ssr.a * p(19u), 0.0, 1.0));
    }
    return vec4f(color, 1.0);
}
)wgsl";

}  // namespace eve::graphics::shaders
