#pragma once

namespace eve::graphics::shaders {

/** @lifetime Shader source has static storage for the process lifetime. */
inline constexpr const char *kReflectionProbeFilterWgsl = R"wgsl(
struct Params { data: vec4f } // face, roughness, diffuse, sampleCount
@group(0) @binding(0) var sourceCube: texture_cube<f32>;
@group(0) @binding(1) var sourceSampler: sampler;
@group(0) @binding(2) var<uniform> params: Params;

struct VSOut { @builtin(position) position: vec4f, @location(0) uv: vec2f };

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> VSOut {
    let positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    var out: VSOut;
    out.position = vec4f(positions[index], 0.0, 1.0);
    out.uv = positions[index] * 0.5 + 0.5;
    return out;
}

fn faceDirection(face: i32, u: f32, v: f32) -> vec3f {
    if (face == 0) { return normalize(vec3f(1.0, -v, -u)); }
    if (face == 1) { return normalize(vec3f(-1.0, -v, u)); }
    if (face == 2) { return normalize(vec3f(u, 1.0, v)); }
    if (face == 3) { return normalize(vec3f(u, -1.0, -v)); }
    if (face == 4) { return normalize(vec3f(u, -v, 1.0)); }
    return normalize(vec3f(-u, -v, -1.0));
}

fn radicalInverse(bitsIn: u32) -> f32 {
    var bits = bitsIn;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn basis(n: vec3f) -> mat3x3f {
    let up = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 0.0, 1.0), abs(n.z) < 0.999);
    let tangent = normalize(cross(up, n));
    return mat3x3f(tangent, cross(n, tangent), n);
}

fn sampleGgx(xi: vec2f, roughness: f32, n: vec3f) -> vec3f {
    let a = max(roughness * roughness, 0.001);
    let phi = 6.28318530718 * xi.x;
    let cosTheta = sqrt((1.0 - xi.y) / max(1.0 + (a * a - 1.0) * xi.y, 0.0001));
    let sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return normalize(basis(n) * vec3f(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta));
}

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let u = in.uv.x * 2.0 - 1.0;
    let v = in.uv.y * 2.0 - 1.0;
    let n = faceDirection(i32(params.data.x + 0.5), u, v);
    let count = clamp(i32(params.data.w + 0.5), 8, 512);
    var radiance = vec3f(0.0);
    var weight = 0.0;
    for (var i = 0; i < 512; i++) {
        if (i >= count) { break; }
        let xi = vec2f(f32(i) / f32(count), radicalInverse(u32(i)));
        var l: vec3f;
        if (params.data.z > 0.5) {
            let phi = 6.28318530718 * xi.x;
            let radius = sqrt(xi.y);
            l = normalize(basis(n) * vec3f(cos(phi) * radius, sin(phi) * radius,
                                           sqrt(max(1.0 - xi.y, 0.0))));
        } else {
            let h = sampleGgx(xi, params.data.y, n);
            l = normalize(reflect(-n, h));
        }
        let noL = max(dot(n, l), 0.0);
        if (noL > 0.0) {
            let sampleWeight = select(noL, 1.0, params.data.z > 0.5);
            radiance += textureSampleLevel(sourceCube, sourceSampler, l, 0.0).rgb * sampleWeight;
            weight += sampleWeight;
        }
    }
    return vec4f(radiance / max(weight, 0.0001), 1.0);
}
)wgsl";

}  // namespace eve::graphics::shaders
