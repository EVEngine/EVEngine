#pragma once

namespace eve::graphics::shaders {

inline constexpr const char *kGrassVertWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame {
    mvp: mat4x4f, model: mat4x4f, lightDir: vec4f, lightColor: vec4f,
    tint: vec4f, cameraPos: vec4f, ambient: vec4f,
    lights: array<Light3D, 8>, texBomb: vec4f, parallax: vec4f,
    surface: vec4f, view: mat4x4f, clipInfo: vec4f, cloud: vec4f, cloudWind: vec4f,
};
struct Externals { data: array<vec4f, 8> };
struct VSIn { @location(0) pos: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f };
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f, @location(1) worldPos: vec3f,
    @location(2) viewPos: vec3f, @location(3) rootPos: vec3f,
    @location(4) instanceId: f32, @location(5) tint: vec4f,
    @location(6) alwaysDark: f32,
};
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(15) var<uniform> params: Externals;
fn p(i: u32) -> f32 { return params.data[i / 4u][i % 4u]; }
fn inverse3(m: mat3x3f) -> mat3x3f {
    let a=m[0].x; let b=m[1].x; let c=m[2].x;
    let d=m[0].y; let e=m[1].y; let f=m[2].y;
    let g=m[0].z; let h=m[1].z; let i=m[2].z;
    let det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    return mat3x3f(vec3f(e*i-f*h,f*g-d*i,d*h-e*g)/det,
                   vec3f(c*h-b*i,a*i-c*g,b*g-a*h)/det,
                   vec3f(b*f-c*e,c*d-a*f,a*e-b*d)/det);
}
@vertex
fn vs_main(in: VSIn) -> VSOut {
    let model3 = mat3x3f(frame.model[0].xyz, frame.model[1].xyz, frame.model[2].xyz);
    let invModel3 = inverse3(model3);
    var up = invModel3 * vec3f(0.0, 1.0, 0.0);
    up = select(vec3f(0.0, 1.0, 0.0), normalize(up), length(up) > 0.00001);
    let modelTranslation = frame.model[3].xyz;
    let cameraObj = invModel3 * (frame.cameraPos.xyz - modelTranslation);
    var toCamera = cameraObj - in.pos;
    toCamera -= up * dot(toCamera, up);
    let fallback = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(up.y) < 0.95);
    let right = select(normalize(cross(up, fallback)), normalize(cross(up, toCamera)),
                       length(toCamera) > 0.0001);
    let scale = max(in.normal.y, 0.05);
    let objectPos = in.pos + right * (in.uv.x - 0.5) * max(p(2u), 0.01) * scale +
                    up * in.uv.y * max(p(3u), 0.01) * scale;
    let world = frame.model * vec4f(objectPos, 1.0);
    let root = frame.model * vec4f(in.pos, 1.0);
    var out: VSOut;
    out.pos = frame.mvp * vec4f(objectPos, 1.0); out.pos.y = -out.pos.y;
    out.uv = in.uv; out.worldPos = world.xyz; out.viewPos = (frame.view * world).xyz;
    out.rootPos = root.xyz; out.instanceId = in.normal.x; out.tint = frame.tint;
    out.alwaysDark = in.normal.z;
    return out;
}
)wgsl";

inline constexpr const char *kGrassFragWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame {
    mvp: mat4x4f, model: mat4x4f, lightDir: vec4f, lightColor: vec4f,
    tint: vec4f, cameraPos: vec4f, ambient: vec4f,
    lights: array<Light3D, 8>, texBomb: vec4f, parallax: vec4f,
    surface: vec4f, view: mat4x4f, clipInfo: vec4f, cloud: vec4f, cloudWind: vec4f,
};
struct ShadowFrame {
    lightVP: array<mat4x4f, 3>, splits: vec4f, bias: vec4f,
    cascadeBias: vec4f, cascadeTexel: vec4f,
};
struct Externals { data: array<vec4f, 8> };
struct FSIn {
    @location(0) uv: vec2f, @location(1) worldPos: vec3f,
    @location(2) viewPos: vec3f, @location(3) rootPos: vec3f,
    @location(4) instanceId: f32, @location(5) tint: vec4f,
    @location(6) alwaysDark: f32,
};
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var albedo: texture_2d<f32>;
@group(0) @binding(4) var<uniform> shadow: ShadowFrame;
@group(0) @binding(5) var shadowMap: texture_depth_2d_array;
@group(0) @binding(7) var mainSampler: sampler;
@group(0) @binding(8) var shadowSampler: sampler_comparison;
@group(0) @binding(15) var<uniform> params: Externals;
fn p(i: u32) -> f32 { return params.data[i / 4u][i % 4u]; }
fn cascadeBias(cascade: i32) -> f32 {
    var value = shadow.cascadeBias.z;
    if (cascade == 0) { value = shadow.cascadeBias.x; }
    if (cascade == 1) { value = shadow.cascadeBias.y; }
    return select(shadow.bias.x, value, value > 0.00000001);
}
fn slopeScaledBias(cascade: i32, nDotL: f32) -> f32 {
    return cascadeBias(cascade) * mix(0.75, 1.0, clamp(nDotL, 0.0, 1.0));
}
fn sampleCascade(worldPos: vec3f, cascade: i32, bias: f32) -> f32 {
    let clip = shadow.lightVP[cascade] * vec4f(worldPos, 1.0);
    let ndc = clip.xyz / max(clip.w, 0.000001);
    // The WebGPU shadow writer mirrors clip Y; sampling must mirror it too.
    let uv = vec2f(ndc.x, -ndc.y) * 0.5 + vec2f(0.5);
    if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0)) || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }
    var sum = 0.0;
    let offsets = array<vec2f, 9>(vec2f(-0.5,-0.5),vec2f(0.0,-0.6),vec2f(0.5,-0.5),
        vec2f(-0.6,0.0),vec2f(0.0),vec2f(0.6,0.0),vec2f(-0.5,0.5),vec2f(0.0,0.6),vec2f(0.5,0.5));
    let dims = vec2f(textureDimensions(shadowMap));
    for (var i = 0; i < 9; i++) {
        let sampleUv = uv + offsets[i] / dims;
        if (all(sampleUv >= vec2f(0.0)) && all(sampleUv <= vec2f(1.0))) {
            sum += textureSampleCompareLevel(shadowMap, shadowSampler, sampleUv, cascade,
                                             ndc.z - bias);
        }
    }
    return sum / 9.0;
}
fn grassShadow(worldPos: vec3f, viewDepth: f32) -> f32 {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 0.0001) { return 1.0; }
    var cascade = 2;
    if (viewDepth < shadow.splits.x) { cascade = 0; }
    else if (viewDepth < shadow.splits.y) { cascade = 1; }
    var texel = shadow.cascadeTexel.z;
    if (cascade == 0) { texel = shadow.cascadeTexel.x; }
    else if (cascade == 1) { texel = shadow.cascadeTexel.y; }
    let normal = vec3f(0.0, 1.0, 0.0);
    let nDotL = max(dot(normal, normalize(frame.lightDir.xyz)), 0.0);
    let samplePos = worldPos + normal * ((2.0 * max(texel, 0.000001)) / max(nDotL, 0.2));
    var visibility = sampleCascade(samplePos, cascade, slopeScaledBias(cascade, nDotL));
    var hi = shadow.splits.z;
    var lo = shadow.splits.y;
    if (cascade == 0) { hi = shadow.splits.x; lo = 0.0; }
    else if (cascade == 1) { hi = shadow.splits.y; lo = shadow.splits.x; }
    let band = max(0.5, (hi - lo) * 0.1);
    let toPrev = 1.0 - clamp((viewDepth - lo) / band, 0.0, 1.0);
    let toNext = 1.0 - clamp((hi - viewDepth) / band, 0.0, 1.0);
    if (toPrev > 0.0 && cascade > 0) {
        visibility = mix(visibility, sampleCascade(samplePos, cascade - 1,
                         slopeScaledBias(cascade - 1, nDotL)), toPrev);
    }
    if (toNext > 0.0 && cascade < 2) {
        visibility = mix(visibility, sampleCascade(samplePos, cascade + 1,
                         slopeScaledBias(cascade + 1, nDotL)), toNext);
    }
    visibility = mix(1.0, visibility, clamp(shadow.splits.w, 0.0, 1.0));
    return mix(0.04, 1.0, visibility);
}
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    let frames = max(p(12u), 1.0); let duration = max(p(1u), 0.0001);
    let random = fract(sin(in.instanceId * 12.9898) * 43758.5453);
    let frameNumber = i32(p(0u) / duration + random * frames) % i32(frames);
    let cols = i32(max(p(13u), 1.0)); let rows = i32(max(p(14u), 1.0));
    let leaf = in.alwaysDark > 0.5 || p(5u) > 0.5;
    let variants = select(i32(max(p(15u), 1.0)), i32(max(p(16u), 1.0)), leaf);
    let variant = i32(in.instanceId) % variants;
    var atlasUv: vec2f;
    if (rows <= 1) {
        atlasUv = vec2f((in.uv.x + f32(frameNumber)) / frames, 1.0 - in.uv.y);
    } else {
        let column = variant * 2 + frameNumber % 2;
        let row = select(0, i32(p(17u) + 0.5), leaf) + frameNumber / 2;
        atlasUv = vec2f((f32(column) + in.uv.x) / f32(cols),
                        (f32(row) + 1.0 - in.uv.y) / f32(rows));
    }
    let mask = textureSample(albedo, mainSampler, atlasUv) * in.tint;
    if (mask.a < max(p(4u), 0.01)) { discard; }
    // Dawn's depth precision self-shadows roots that lie exactly on the ground
    // plane. Sample the visible blade point; the explicit normal offset in
    // grassShadow still preserves occluder shadows without blanket acne.
    var visibility = grassShadow(in.worldPos, max(-in.viewPos.z, 0.0));
    if (p(5u) > 0.5 || in.alwaysDark > 0.5) { visibility = 0.0; }
    let color = mix(vec3f(p(9u), p(10u), p(11u)), vec3f(p(6u), p(7u), p(8u)), visibility);
    return vec4f(color * mask.rgb, mask.a);
}
)wgsl";

}  // namespace eve::graphics::shaders
