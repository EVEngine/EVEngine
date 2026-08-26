#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kWaterfallFragWgsl = R"wgsl(
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
@group(0) @binding(7) var mainSampler: sampler;
@group(0) @binding(15) var<uniform> params: Externals;
fn p(i: u32) -> f32 { return params.data[i / 4u][i % 4u]; }
fn hash(n: f32) -> f32 { return fract(sin(n) * 43758.5453123); }
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
    let lod = min(1.0 + (1.0 - ndv) * 3.0,
                  f32(max(i32(textureNumLevels(environment)) - 1, 0)));
    let reflection = textureSampleLevel(environment, mainSampler, reflect(-v, n), lod).rgb;
    var color = mix(vec3f(p(10u), p(11u), p(12u)), reflection,
                    clamp((0.02 + 0.98 * pow(1.0 - ndv, 5.0)) * p(8u) + 0.05, 0.0, 1.0));
    let h = normalize(v + normalize(frame.lightDir.xyz));
    color += frame.lightColor.rgb * pow(max(dot(n, h), 0.0), 96.0) * p(9u);
    let topBand = clamp((in.uv.y - (1.0 - p(5u))) / max(p(5u), 0.0001), 0.0, 1.0);
    let bottomBand = clamp((p(6u) - in.uv.y) / max(p(6u), 0.0001), 0.0, 1.0);
    let foam = clamp(cascade(in.uv, time) * p(7u) +
                     clamp(topBand + bottomBand, 0.0, 1.0) * 0.9, 0.0, 1.0);
    color = mix(color, vec3f(0.90, 0.95, 1.0), foam);
    return vec4f(color, 1.0 - bottomBand * 0.5);
}
)wgsl";

}  // namespace eve::graphics::shaders
