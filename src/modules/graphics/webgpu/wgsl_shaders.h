#pragma once
// WGSL shader sources for the WebGPU graphics backend.
// WebGPU has no push constants or combined image samplers, so every pipeline
// binds uniform buffers / textures / samplers explicitly through bind groups.
// The memory layouts match the Vulkan GLSL sources (std140) exactly so the
// existing C++ UBO structs (Mesh3DUBO, ShadowUBO, Lighting2DUBO) are reused.

namespace eve::graphics::webgpu {

// ---- 2D solid (color) -----------------------------------------------------
inline const char *kColorVertWgsl = R"wgsl(
struct VSIn {
    @location(0) pos: vec2f,
    @location(1) color: vec4f,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
};
@vertex
fn vs_main(in: VSIn) -> VSOut {
    var out: VSOut;
    out.pos = vec4f(in.pos, 0.0, 1.0);
    out.color = in.color;
    return out;
}
)wgsl";

inline const char *kColorFragWgsl = R"wgsl(
struct FSIn {
    @location(0) color: vec4f,
};
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    return in.color;
}
)wgsl";

// ---- 2D textured ----------------------------------------------------------
inline const char *kTexturedVertWgsl = R"wgsl(
struct VSIn {
    @location(0) pos: vec2f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};
@vertex
fn vs_main(in: VSIn) -> VSOut {
    var out: VSOut;
    out.pos = vec4f(in.pos, 0.0, 1.0);
    out.color = in.color;
    out.uv = in.uv;
    return out;
}
)wgsl";

inline const char *kTexturedFragWgsl = R"wgsl(
struct FSIn {
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    return textureSample(mainTex, mainSamp, in.uv) * in.color;
}
)wgsl";

// ---- 2D custom / post (Externals UBO: float data[32] == Shader push block) -
inline const char *kCustom2DFragWgsl = R"wgsl(
struct FSIn {
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};
struct Externals {
    data: array<f32, 32>,
};
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@group(0) @binding(4) var<uniform> u: Externals;
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    return textureSample(mainTex, mainSamp, in.uv) * in.color;
}
)wgsl";

// ---- 2D lit ---------------------------------------------------------------
inline const char *kLit2DFragWgsl = R"wgsl(
struct FSIn {
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};
struct Light2D {
    posRadius: vec4f,
    color: vec4f,
};
struct Lighting2D {
    ambient: vec4f,   // rgb ambient
    lightInfo: vec4f, // x = count, y = viewW, z = viewH
    lights: array<Light2D, 8>,
};
@group(0) @binding(0) var albedoTex: texture_2d<f32>;
@group(0) @binding(1) var normalTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@group(0) @binding(4) var<uniform> u: Lighting2D;
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    let base = textureSample(albedoTex, mainSamp, in.uv) * in.color;
    var lit = base.rgb * u.ambient.rgb;
    let count = i32(u.lightInfo.x + 0.5);
    let view = vec2f(u.lightInfo.y, u.lightInfo.z);
    for (var i = 0; i < 8; i = i + 1) {
        if (i >= count) { break; }
        let l = u.lights[i];
        let px = in.uv * view;
        if (l.posRadius.w <= 0.0) {
            // Directional: light travels along posRadius.xy
            let ndl = max(dot(vec3f(0.0, 0.0, 1.0), normalize(vec3f(l.posRadius.xy, 1.0))), 0.0);
            lit += base.rgb * l.color.rgb * ndl;
        } else {
            let toL = l.posRadius.xy - px;
            let dist = length(toL);
            let att = clamp(1.0 - dist / max(l.posRadius.w, 1e-3), 0.0, 1.0);
            lit += base.rgb * l.color.rgb * att * att;
        }
    }
    return vec4f(lit, base.a);
}
)wgsl";

// ---- Mesh3D (full PBR, layout matches Mesh3DUBO / ShadowUBO) --------------
inline const char *kMesh3DVertWgsl = R"wgsl(
struct VSIn {
    @location(0) pos: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct Light3D {
    posRadius: vec4f,
    color: vec4f,
};
struct Frame {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    tint: vec4f,
    cameraPos: vec4f,
    ambient: vec4f,
    lights: array<Light3D, 8>,
    texBomb: vec4f,
    parallax: vec4f,
    view: mat4x4f,
    clipInfo: vec4f,
    cloud: vec4f,
    cloudWind: vec4f,
};

struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
    @location(5) vViewPos: vec3f,
};
@group(0) @binding(0) var<uniform> ubo: Frame;
// WGSL has no stdlib inverse(); implement 3x3 inverse for the normal matrix.
fn inverse3x3(m: mat3x3f) -> mat3x3f {
    let a = m[0].x; let b = m[1].x; let c = m[2].x;
    let d = m[0].y; let e = m[1].y; let f = m[2].y;
    let g = m[0].z; let h = m[1].z; let i = m[2].z;
    let det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return mat3x3f(
        vec3f((e * i - f * h) / det, (f * g - d * i) / det, (d * h - e * g) / det),
        vec3f((c * h - b * i) / det, (a * i - c * g) / det, (b * g - a * h) / det),
        vec3f((b * f - c * e) / det, (c * d - a * f) / det, (a * e - b * d) / det),
    );
}
@vertex
fn vs_main(in: VSIn) -> VSOut {
    var out: VSOut;
    out.pos = ubo.mvp * vec4f(in.pos, 1.0);
    let world = ubo.model * vec4f(in.pos, 1.0);
    out.vWorldPos = world.xyz;
    out.vViewPos = (ubo.view * world).xyz;
    let nrm = transpose(inverse3x3(mat3x3f(ubo.model[0].xyz, ubo.model[1].xyz, ubo.model[2].xyz))) * in.normal;
    out.vNormal = normalize(nrm);
    out.vUV = in.uv;
    out.vTint = ubo.tint;
    out.vCameraPos = ubo.cameraPos.xyz;
    return out;
}
)wgsl";

inline const char *kMesh3DFragWgsl = R"wgsl(
struct Light3D {
    posRadius: vec4f,
    color: vec4f,
};
struct Frame {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    tint: vec4f,
    cameraPos: vec4f,
    ambient: vec4f,
    lights: array<Light3D, 8>,
    texBomb: vec4f,
    parallax: vec4f,
    view: mat4x4f,
    clipInfo: vec4f,
    cloud: vec4f,
    cloudWind: vec4f,
};
struct ShadowFrame {
    lightVP: array<mat4x4f, 3>,
    splits: vec4f,
    bias: vec4f,
    cascadeBias: vec4f,
    cascadeTexel: vec4f,
};
struct FSIn {
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
    @location(5) vViewPos: vec3f,
    @builtin(position) fragCoord: vec4f,
};
@group(0) @binding(0) var<uniform> ubo: Frame;
@group(0) @binding(1) var albedoSampler: texture_2d<f32>;
@group(0) @binding(2) var normalSampler: texture_2d<f32>;
@group(0) @binding(3) var envSampler: texture_cube<f32>;
@group(0) @binding(4) var<uniform> shadow: ShadowFrame;
@group(0) @binding(5) var shadowMap: texture_depth_2d_array;
@group(0) @binding(6) var heightSampler: texture_2d<f32>;
@group(0) @binding(7) var mainSamp: sampler;
@group(0) @binding(8) var shadowSamp: sampler_comparison;
@group(0) @binding(10) var aoTex: texture_2d<f32>;
@group(0) @binding(11) var aoSamp: sampler;

const PI: f32 = 3.14159265359;

fn distGGX(n: vec3f, h: vec3f, rough: f32) -> f32 {
    let a = max(rough * rough, 0.002);
    let a2 = a * a;
    let ndh = max(dot(n, h), 0.0);
    let denom = (ndh * ndh * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}
fn geomSchlick(ndv: f32, rough: f32) -> f32 {
    let r = rough + 1.0;
    let k = (r * r) / 8.0;
    return ndv / max(ndv * (1.0 - k) + k, 1e-4);
}
fn geomSmith(n: vec3f, v: vec3f, l: vec3f, rough: f32) -> f32 {
    return geomSchlick(max(dot(n, v), 0.0), rough) * geomSchlick(max(dot(n, l), 0.0), rough);
}
fn fresnelSchlick(cosT: f32, f0: vec3f) -> vec3f {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}
fn shadeLight(n: vec3f, v: vec3f, albedo: vec3f, metallic: f32, rough: f32, l: vec3f, rad: vec3f) -> vec3f {
    let ndl = max(dot(n, l), 0.0);
    let diffuse = mix(ndl, ndl * 0.5 + 0.5, 0.25);
    let h = normalize(v + l);
    let f0 = mix(vec3f(0.04), albedo, metallic);
    let ndf = distGGX(n, h, rough);
    let g = geomSmith(n, v, l, rough);
    let f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    let spec = (ndf * g * f) / max(4.0 * max(dot(n, v), 0.0) * max(ndl, 0.001), 1e-3);
    let kd = (vec3f(1.0) - f) * (1.0 - metallic);
    return (kd * albedo * diffuse + spec * ndl) * rad;
}
fn cloudHash(p: vec2f) -> f32 {
    let ip = vec2i(floor(p % 64.0));
    var h = (u32(ip.x) * 374761393u) ^ (u32(ip.y) * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return f32(h & 0x00FFFFFFu) / f32(0x00FFFFFFu);
}
fn cloudNoise(p: vec2f) -> f32 {
    let i = floor(p);
    var f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    let a = cloudHash(i);
    let b = cloudHash(i + vec2f(1.0, 0.0));
    let c = cloudHash(i + vec2f(0.0, 1.0));
    let d = cloudHash(i + vec2f(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
fn cloudFbm(p: vec2f) -> f32 {
    var sum = 0.0;
    var amp = 0.5;
    var freq = 1.0;
    for (var i = 0; i < 4; i = i + 1) {
        sum += amp * cloudNoise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return sum * 2.0;
}
fn cloudShadowFactor(worldPos: vec3f) -> f32 {
    if (ubo.cloud.x < 1e-4) { return 1.0; }
    let cell = 1.0 / max(ubo.cloud.y, 1e-4);
    let drift = (ubo.cloudWind.xy * cell) * ubo.cloud.z;
    let p = worldPos.xz * cell - drift;
    let f = cloudFbm(p);
    let c = smoothstep(ubo.cloudWind.z - 0.1, ubo.cloudWind.z + 0.1, f);
    let d = cloudNoise(p * 3.0 + vec2f(11.7, 5.3));
    let covered = mix(c, c * d, ubo.cloudWind.w);
    return 1.0 - clamp(covered, 0.0, 1.0) * clamp(ubo.cloud.x, 0.0, 1.0);
}
fn sampleShadowCascade(worldPos: vec3f, cascade: i32, bias: f32) -> f32 {    let lightClip = shadow.lightVP[cascade] * vec4f(worldPos, 1.0);
    let ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    let uv = ndc.xy * 0.5 + 0.5;
    let depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0) {
        return 1.0;
    }
    let texel = 1.0 / vec2f(textureDimensions(shadowMap));
    var sum: f32 = 0.0;
    let zref = depth - bias;
    for (var i = 0; i < 9; i = i + 1) {
        var s: vec2f;
        if (i == 0) { s = uv + vec2f(-0.5, -0.5) * texel; }
        else if (i == 1) { s = uv + vec2f(0.0, -0.6) * texel; }
        else if (i == 2) { s = uv + vec2f(0.5, -0.5) * texel; }
        else if (i == 3) { s = uv + vec2f(-0.6, 0.0) * texel; }
        else if (i == 4) { s = uv; }
        else if (i == 5) { s = uv + vec2f(0.6, 0.0) * texel; }
        else if (i == 6) { s = uv + vec2f(-0.5, 0.5) * texel; }
        else if (i == 7) { s = uv + vec2f(0.0, 0.6) * texel; }
        else { s = uv + vec2f(0.5, 0.5) * texel; }
        if (s.x >= 0.0 && s.x <= 1.0 && s.y >= 0.0 && s.y <= 1.0) {
            sum += textureSampleCompareLevel(shadowMap, shadowSamp, s, cascade, zref);
        }
    }
    return sum / 9.0;
}
fn sampleShadowPCF(worldPos: vec3f, n: vec3f, viewDepth: f32, ndl: f32) -> f32 {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4) {
        return 1.0;
    }
    var cascade: i32 = 2;
    if (viewDepth < shadow.splits.x) { cascade = 0; }
    else if (viewDepth < shadow.splits.y) { cascade = 1; }
    var tw: f32;
    if (cascade == 0) { tw = shadow.cascadeTexel.x; }
    else if (cascade == 1) { tw = shadow.cascadeTexel.y; }
    else { tw = shadow.cascadeTexel.z; }
    var cb: f32;
    if (cascade == 0) { cb = shadow.cascadeBias.x; }
    else if (cascade == 1) { cb = shadow.cascadeBias.y; }
    else { cb = shadow.cascadeBias.z; }
    if (cb < 1e-8) { cb = shadow.bias.x; }
    let bias = cb * mix(0.75, 1.0, clamp(ndl, 0.0, 1.0));
    let p = worldPos + n * ((2.0 * max(tw, 1e-6)) / max(ndl, 0.2));
    let vis = sampleShadowCascade(p, cascade, bias);
    return mix(0.04, 1.0, vis);
}
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    var nGeom = normalize(in.vNormal);
    let v = normalize(in.vCameraPos - in.vWorldPos);
    if (dot(nGeom, v) < 0.0) { nGeom = -nGeom; }
    let base = textureSample(albedoSampler, mainSamp, in.vUV) * in.vTint;
    if (base.a < 0.5) { discard; }
    let albedo = base.rgb;
    let metallic = clamp(ubo.ambient.w, 0.0, 1.0);
    let rough = clamp(ubo.cameraPos.w, 0.04, 1.0);
    let count = i32(ubo.lightDir.w + 0.5);
    let nSmp = textureSample(normalSampler, mainSamp, in.vUV).xyz;
    var n = nGeom;
    if (length(nSmp - vec3f(0.5, 0.5, 1.0)) > 0.04) {
        let mapN = nSmp * 2.0 - 1.0;
        n = normalize(mapN);
    }
    var lo = vec3f(0.0);
    let viewDepth = max(-in.vViewPos.z, 0.0);
    let primaryL = normalize(ubo.lightDir.xyz);
    let shadowVis = sampleShadowPCF(in.vWorldPos, n, viewDepth, max(dot(n, primaryL), 0.0));
    if (length(ubo.lightColor.rgb) > 1e-6) {
        lo += shadeLight(n, v, albedo, metallic, rough, primaryL, ubo.lightColor.rgb) * shadowVis *
              cloudShadowFactor(in.vWorldPos);
    }
    for (var i = 0; i < 8; i = i + 1) {
        if (i >= count) { break; }
        let lgt = ubo.lights[i];
        var rad = lgt.color.rgb;
        var l: vec3f;
        if (lgt.posRadius.w <= 0.0) {
            l = normalize(lgt.posRadius.xyz);
            if (length(l - primaryL) < 1e-3) { continue; }
        } else {
            let toL = lgt.posRadius.xyz - in.vWorldPos;
            let dist = length(toL);
            l = toL / max(dist, 1e-4);
            let atten = clamp(1.0 - dist / max(lgt.posRadius.w, 1e-3), 0.0, 1.0);
            rad *= atten * atten;
        }
        lo += shadeLight(n, v, albedo, metallic, rough, l, rad);
    }
    let hemi = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    let skyIrr = ubo.ambient.rgb * 1.1 + ubo.lightColor.rgb * 0.12;
    let gndIrr = ubo.ambient.rgb * vec3f(0.72, 0.62, 0.52);
    let irr = mix(gndIrr, skyIrr, hemi);
    var color = albedo * irr * (1.0 - metallic) + lo;
    let envIntensity = ubo.lightColor.w;
    if (envIntensity > 1e-4) {
        let r = reflect(-v, n);
        let envSpec = textureSampleLevel(envSampler, mainSamp, r, rough * 5.0).rgb * envIntensity;
        let f0 = mix(vec3f(0.04), albedo, metallic);
        let f = fresnelSchlick(max(dot(n, v), 0.0), f0);
        color += envSpec * f;
        let irr2 = textureSampleLevel(envSampler, mainSamp, n, 5.0).rgb * envIntensity;
        color += albedo * irr2 * (1.0 - metallic) * (1.0 - f) * 0.45;
    }
    // Screen-space ambient occlusion (G-buffer SSAO pass output; strength in
    // texBomb.w is 0 when AO is disabled via RenderControl, which also keeps
    // the binding white in that case).
    let aoUV = in.fragCoord.xy / vec2f(textureDimensions(aoTex));
    let ao = textureSampleLevel(aoTex, aoSamp, aoUV, 0.0).r;
    color *= mix(1.0, ao, clamp(ubo.texBomb.w, 0.0, 1.0));
    // Match the Vulkan tonemap.glsl: keep values below `white` linear so dim
    // scenes stay readable, compress only the HDR remainder into (white, 1].
    let white = 0.85;
    let over = max(color - vec3f(white), vec3f(0.0));
    color = min(color, vec3f(white)) + vec3f(1.0 - white) * (over / (over + vec3f(1.0)));
    // The scene texture's alpha would otherwise carry linear depth, which the
    // swapchain compositor blends as transparency (making the mesh nearly
    // invisible). Output opaque.
    return vec4f(color, 1.0);
}
)wgsl";

// ---- Mesh3D clustered forward (matches Mesh3DClusteredUBO) -----------------
inline const char *kMesh3DClusteredVertWgsl = R"wgsl(
struct VSIn {
    @location(0) pos: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct Frame {
    mvp: mat4x4f,
    model: mat4x4f,
    view: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    tint: vec4f,
    cameraPos: vec4f,
    ambient: vec4f,
    gridInfo: vec4f,
    clipInfo: vec4f,
    texBomb: vec4f,
    parallax: vec4f,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
    @location(5) vViewPos: vec3f,
};
@group(0) @binding(0) var<uniform> ubo: Frame;
fn inverse3x3(m: mat3x3f) -> mat3x3f {
    let a = m[0].x; let b = m[1].x; let c = m[2].x;
    let d = m[0].y; let e = m[1].y; let f = m[2].y;
    let g = m[0].z; let h = m[1].z; let i = m[2].z;
    let det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return mat3x3f(
        vec3f((e * i - f * h) / det, (f * g - d * i) / det, (d * h - e * g) / det),
        vec3f((c * h - b * i) / det, (a * i - c * g) / det, (b * g - a * h) / det),
        vec3f((b * f - c * e) / det, (c * d - a * f) / det, (a * e - b * d) / det),
    );
}
@vertex
fn vs_main(in: VSIn) -> VSOut {
    var out: VSOut;
    out.pos = ubo.mvp * vec4f(in.pos, 1.0);
    let world = ubo.model * vec4f(in.pos, 1.0);
    out.vWorldPos = world.xyz;
    out.vViewPos = (ubo.view * world).xyz;
    let nrm = transpose(inverse3x3(mat3x3f(ubo.model[0].xyz, ubo.model[1].xyz, ubo.model[2].xyz))) * in.normal;
    out.vNormal = normalize(nrm);
    out.vUV = in.uv;
    out.vTint = ubo.tint;
    out.vCameraPos = ubo.cameraPos.xyz;
    return out;
}
)wgsl";

inline const char *kMesh3DClusteredFragWgsl = R"wgsl(
struct Light3D {
    posRadius: vec4f,
    color: vec4f,
};
struct Frame {
    mvp: mat4x4f,
    model: mat4x4f,
    view: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    tint: vec4f,
    cameraPos: vec4f,
    ambient: vec4f,
    gridInfo: vec4f,
    clipInfo: vec4f,
    texBomb: vec4f,
    parallax: vec4f,
};
struct ShadowFrame {
    lightVP: array<mat4x4f, 3>,
    splits: vec4f,
    bias: vec4f,
    cascadeBias: vec4f,
    cascadeTexel: vec4f,
};
struct FSIn {
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
    @location(5) vViewPos: vec3f,
    @builtin(position) fragCoord: vec4f,
};
@group(0) @binding(0) var<uniform> ubo: Frame;
@group(0) @binding(1) var albedoSampler: texture_2d<f32>;
@group(0) @binding(2) var normalSampler: texture_2d<f32>;
@group(0) @binding(3) var envSampler: texture_cube<f32>;
@group(0) @binding(4) var<uniform> shadow: ShadowFrame;
@group(0) @binding(5) var shadowMap: texture_depth_2d_array;
@group(0) @binding(6) var heightSampler: texture_2d<f32>;
@group(0) @binding(7) var mainSamp: sampler;
@group(0) @binding(8) var shadowSamp: sampler_comparison;
@group(0) @binding(9) var sceneDepth: texture_depth_2d;
@group(0) @binding(10) var<storage, read> lights: array<Light3D>;
@group(0) @binding(11) var<storage, read> clusterTable: array<vec2u>;
@group(0) @binding(12) var<storage, read> lightIndices: array<u32>;
@group(0) @binding(13) var aoTex: texture_2d<f32>;
@group(0) @binding(14) var aoSamp: sampler;

const PI: f32 = 3.14159265359;

fn distGGX(n: vec3f, h: vec3f, rough: f32) -> f32 {
    let a = max(rough * rough, 0.002);
    let a2 = a * a;
    let ndh = max(dot(n, h), 0.0);
    let denom = (ndh * ndh * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}
fn geomSchlick(ndv: f32, rough: f32) -> f32 {
    let r = rough + 1.0;
    let k = (r * r) / 8.0;
    return ndv / max(ndv * (1.0 - k) + k, 1e-4);
}
fn geomSmith(n: vec3f, v: vec3f, l: vec3f, rough: f32) -> f32 {
    return geomSchlick(max(dot(n, v), 0.0), rough) * geomSchlick(max(dot(n, l), 0.0), rough);
}
fn fresnelSchlick(cosT: f32, f0: vec3f) -> vec3f {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}
fn shadeLight(n: vec3f, v: vec3f, albedo: vec3f, metallic: f32, rough: f32, l: vec3f, rad: vec3f) -> vec3f {
    let ndl = max(dot(n, l), 0.0);
    let diffuse = mix(ndl, ndl * 0.5 + 0.5, 0.25);
    let h = normalize(v + l);
    let f0 = mix(vec3f(0.04), albedo, metallic);
    let ndf = distGGX(n, h, rough);
    let g = geomSmith(n, v, l, rough);
    let f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    let spec = (ndf * g * f) / max(4.0 * max(dot(n, v), 0.0) * max(ndl, 0.001), 1e-3);
    let kd = (vec3f(1.0) - f) * (1.0 - metallic);
    return (kd * albedo * diffuse + spec * ndl) * rad;
}
fn sampleShadowCascade(worldPos: vec3f, cascade: i32, bias: f32) -> f32 {
    let lightClip = shadow.lightVP[cascade] * vec4f(worldPos, 1.0);
    let ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    let uv = ndc.xy * 0.5 + 0.5;
    let depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0) {
        return 1.0;
    }
    let texel = 1.0 / vec2f(textureDimensions(shadowMap));
    var sum: f32 = 0.0;
    let zref = depth - bias;
    for (var i = 0; i < 9; i = i + 1) {
        var s: vec2f;
        if (i == 0) { s = uv + vec2f(-0.5, -0.5) * texel; }
        else if (i == 1) { s = uv + vec2f(0.0, -0.6) * texel; }
        else if (i == 2) { s = uv + vec2f(0.5, -0.5) * texel; }
        else if (i == 3) { s = uv + vec2f(-0.6, 0.0) * texel; }
        else if (i == 4) { s = uv; }
        else if (i == 5) { s = uv + vec2f(0.6, 0.0) * texel; }
        else if (i == 6) { s = uv + vec2f(-0.5, 0.5) * texel; }
        else if (i == 7) { s = uv + vec2f(0.0, 0.6) * texel; }
        else { s = uv + vec2f(0.5, 0.5) * texel; }
        if (s.x >= 0.0 && s.x <= 1.0 && s.y >= 0.0 && s.y <= 1.0) {
            sum += textureSampleCompareLevel(shadowMap, shadowSamp, s, cascade, zref);
        }
    }
    return sum / 9.0;
}
fn sampleShadowPCF(worldPos: vec3f, n: vec3f, viewDepth: f32, ndl: f32) -> f32 {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4) {
        return 1.0;
    }
    var cascade: i32 = 2;
    if (viewDepth < shadow.splits.x) { cascade = 0; }
    else if (viewDepth < shadow.splits.y) { cascade = 1; }
    var tw: f32;
    if (cascade == 0) { tw = shadow.cascadeTexel.x; }
    else if (cascade == 1) { tw = shadow.cascadeTexel.y; }
    else { tw = shadow.cascadeTexel.z; }
    var cb: f32;
    if (cascade == 0) { cb = shadow.cascadeBias.x; }
    else if (cascade == 1) { cb = shadow.cascadeBias.y; }
    else { cb = shadow.cascadeBias.z; }
    if (cb < 1e-8) { cb = shadow.bias.x; }
    let bias = cb * mix(0.75, 1.0, clamp(ndl, 0.0, 1.0));
    let p = worldPos + n * ((2.0 * max(tw, 1e-6)) / max(ndl, 0.2));
    let vis = sampleShadowCascade(p, cascade, bias);
    return mix(0.04, 1.0, vis);
}
fn clusterIndex(frag: vec2f, viewDepth: f32) -> u32 {
    let tilesX = i32(ubo.gridInfo.x + 0.5);
    let tilesY = i32(ubo.gridInfo.y + 0.5);
    let slices = i32(ubo.gridInfo.z + 0.5);
    let nearZ = max(ubo.clipInfo.x, 1e-3);
    let farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    let screenW = max(ubo.clipInfo.z, 1.0);
    let screenH = max(ubo.clipInfo.w, 1.0);
    let tx = clamp(i32(floor(frag.x / screenW * f32(tilesX))), 0, tilesX - 1);
    let ty = clamp(i32(floor(frag.y / screenH * f32(tilesY))), 0, tilesY - 1);
    let depth = max(viewDepth, nearZ);
    let sz = clamp(i32(floor((depth - nearZ) / (farZ - nearZ) * f32(slices))), 0, slices - 1);
    return u32((sz * tilesY + ty) * tilesX + tx);
}
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    var nGeom = normalize(in.vNormal);
    let v = normalize(in.vCameraPos - in.vWorldPos);
    if (dot(nGeom, v) < 0.0) { nGeom = -nGeom; }
    let base = textureSample(albedoSampler, mainSamp, in.vUV) * in.vTint;
    if (base.a < 0.5) { discard; }
    let albedo = base.rgb;
    let metallic = clamp(ubo.ambient.w, 0.0, 1.0);
    let rough = clamp(ubo.cameraPos.w, 0.04, 1.0);
    let nSmp = textureSample(normalSampler, mainSamp, in.vUV).xyz;
    var n = nGeom;
    if (length(nSmp - vec3f(0.5, 0.5, 1.0)) > 0.04) {
        n = normalize(nSmp * 2.0 - 1.0);
    }
    var lo = vec3f(0.0);
    let viewDepth = max(-in.vViewPos.z, 0.0);
    let primaryL = normalize(ubo.lightDir.xyz);
    let shadowVis = sampleShadowPCF(in.vWorldPos, n, viewDepth, max(dot(n, primaryL), 0.0));
    if (ubo.lightDir.w > 0.5 && length(ubo.lightColor.rgb) > 1e-6) {
        lo += shadeLight(n, v, albedo, metallic, rough, primaryL, ubo.lightColor.rgb) * shadowVis;
    }
    let cid = clusterIndex(in.fragCoord.xy, viewDepth);
    let entry = clusterTable[cid];
    let count = min(entry.y, 32u);
    for (var i = 0u; i < count; i = i + 1u) {
        let li = lightIndices[entry.x + i];
        let lgt = lights[li];
        let toL = lgt.posRadius.xyz - in.vWorldPos;
        let dist = length(toL);
        let l = toL / max(dist, 1e-4);
        let atten = clamp(1.0 - dist / max(lgt.posRadius.w, 1e-3), 0.0, 1.0);
        lo += shadeLight(n, v, albedo, metallic, rough, l, lgt.color.rgb * atten * atten);
    }
    let hemi = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    let skyIrr = ubo.ambient.rgb * 1.1 + ubo.lightColor.rgb * 0.12;
    let gndIrr = ubo.ambient.rgb * vec3f(0.72, 0.62, 0.52);
    let irr = mix(gndIrr, skyIrr, hemi);
    var color = albedo * irr * (1.0 - metallic) + lo;
    let envIntensity = ubo.lightColor.w;
    if (envIntensity > 1e-4) {
        let r = reflect(-v, n);
        let envSpec = textureSampleLevel(envSampler, mainSamp, r, rough * 5.0).rgb * envIntensity;
        let f0 = mix(vec3f(0.04), albedo, metallic);
        let f = fresnelSchlick(max(dot(n, v), 0.0), f0);
        color += envSpec * f;
        let irr2 = textureSampleLevel(envSampler, mainSamp, n, 5.0).rgb * envIntensity;
        color += albedo * irr2 * (1.0 - metallic) * (1.0 - f) * 0.45;
    }
    let aoUV = in.fragCoord.xy / vec2f(textureDimensions(aoTex));
    let ao = textureSampleLevel(aoTex, aoSamp, aoUV, 0.0).r;
    color *= mix(1.0, ao, clamp(ubo.texBomb.w, 0.0, 1.0));
    let white = 0.85;
    let over = max(color - vec3f(white), vec3f(0.0));
    color = min(color, vec3f(white)) + vec3f(1.0 - white) * (over / (over + vec3f(1.0)));
    return vec4f(color, 1.0);
}
)wgsl";

// ---- Mesh3D depth-only shadow --------------------------------------------------
inline const char *kMesh3DShadowVertWgsl = R"wgsl(
struct VSIn {
    @location(0) pos: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct Push {
    mvp: mat4x4f,
};
@group(0) @binding(0) var<uniform> pc: Push;
@vertex
fn vs_main(in: VSIn) -> @builtin(position) vec4f {
    return pc.mvp * vec4f(in.pos, 1.0);
}
)wgsl";

// ---- GBuffer pass (normal / linear-depth / albedo) -------------------------
inline const char *kMesh3DGbufferVertWgsl = R"wgsl(
struct VSIn {
    @location(0) pos: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct Push {
    mvp: mat4x4f,
    modelR0: vec4f,
    modelR1: vec4f,
    modelR2: vec4f,
    clip: vec4f,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) vNormal: vec3f,
    @location(1) vNdcZ: f32,
    @location(2) vUV: vec2f,
};
@group(0) @binding(0) var<uniform> pc: Push;
@vertex
fn vs_main(in: VSIn) -> VSOut {
    var out: VSOut;
    out.pos = pc.mvp * vec4f(in.pos, 1.0);
    let nrm = mat3x3f(pc.modelR0.xyz, pc.modelR1.xyz, pc.modelR2.xyz) * in.normal;
    out.vNormal = normalize(nrm);
    out.vNdcZ = out.pos.z;
    out.vUV = in.uv;
    return out;
}
)wgsl";

inline const char *kMesh3DGbufferFragWgsl = R"wgsl(
struct FSIn {
    @location(0) vNormal: vec3f,
    @location(1) vNdcZ: f32,
    @location(2) vUV: vec2f,
};
struct Push {
    mvp: mat4x4f,
    modelR0: vec4f,
    modelR1: vec4f,
    modelR2: vec4f,
    clip: vec4f,
};
struct GBufOut {
    @location(0) normal: vec4f,
    @location(1) depthColor: vec4f,
    @location(2) albedo: vec4f,
};
@group(0) @binding(0) var<uniform> pc: Push;
@group(0) @binding(1) var albedoSampler: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@fragment
fn fs_main(in: FSIn) -> GBufOut {
    var out: GBufOut;
    out.normal = vec4f(in.vNormal * 0.5 + 0.5, 1.0);
    let nearZ = max(pc.clip.x, 1e-4);
    let farZ = max(pc.clip.y, nearZ + 1e-3);
    // NDC z -> linear [0,1] over the clip range (matches scene color A).
    let ndc = clamp(in.vNdcZ, 0.0, 1.0);
    let linear = nearZ * farZ / max(farZ - ndc * (farZ - nearZ), 1e-6);
    out.depthColor = vec4f(linear, linear, linear, 1.0);
    out.albedo = textureSample(albedoSampler, mainSamp, in.vUV);
    return out;
}
)wgsl";

// ---- SSAO (G-buffer depth based, cheap screen-space) -----------------------
inline const char *kSSAOFragWgsl = R"wgsl(
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(1) uv: vec2f,
};
struct AOUniforms {
    params: vec4f,    // x = radius (UV offset scale), y = power, z = nearZ (hw), w = farZ (hw)
    intensity: f32,   // 0 = off
    invScale: f32,    // AO target size / depth size (0.5 = half-res AO)
    _pad: f32,
};
@group(0) @binding(0) var<uniform> ubo: AOUniforms;
@group(0) @binding(1) var depthTex: texture_depth_2d;
@group(0) @binding(2) var samp: sampler;

fn hash12(p: vec2f) -> f32 {
    return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}

fn linearizeDepth(z: f32) -> f32 {
    let nearZ = ubo.params.z;
    let farZ = ubo.params.w;
    return nearZ * farZ / max(farZ - z * (farZ - nearZ), 1e-6);
}

@fragment
fn fs_main(in: VSOut) -> @location(0) vec4f {
    let dims = vec2f(textureDimensions(depthTex));
    // AO runs at half resolution; the upsampled bilinear fetch in the mesh
    // pass acts as a cheap 2x2 blur, which removes the per-pixel grain.
    let uv = (in.pos.xy + vec2f(0.5)) / (dims * ubo.invScale);
    // Hardware depth (Depth32Float, NDC z in [0,1], 0 = near, 1 = far) —
    // linearized to world units so the occlusion delta is not crushed by
    // perspective (hw depth deltas near the far plane are ~1e-4).
    let rawZ = textureLoad(depthTex, vec2<i32>(uv * dims), 0);
    let centerDepth = linearizeDepth(rawZ);
    let nearZ = ubo.params.z;
    let farZ = ubo.params.w;
    if (centerDepth >= farZ * 0.99 || centerDepth <= nearZ * 1.02) {
        return vec4f(1.0, 1.0, 1.0, 1.0);
    }
    // Screen radius scales with the compressed depth: near surfaces span more
    // pixels for the same world-space radius.
    let rad = ubo.params.x / (rawZ + 0.05);
    var occ = 0.0;
    for (var i = 0; i < 12; i = i + 1) {
        let f = (f32(i) + 0.5) / 12.0;
        let ang = 6.2831853 * f + hash12(uv * 311.7 + vec2f(f32(i), 0.0)) * 0.9;
        let r = rad * (0.25 + 0.75 * f);
        let sampleUV = clamp(uv + vec2f(cos(ang), sin(ang)) * r, vec2f(0.001), vec2f(0.999));
        let sampleDepth = linearizeDepth(textureLoad(depthTex, vec2<i32>(sampleUV * dims), 0));
        if (sampleDepth >= farZ * 0.99) { continue; }
        // The sample is occluded when it is closer than the center surface
        // (positive diff in world units); smoothstep softens the hard cutoff
        // that turned tiny depth deltas into per-pixel speckle.
        let diff = centerDepth - sampleDepth;
        let delta = diff / max(centerDepth, 1e-4);
        occ += smoothstep(0.0, 0.4, delta * 35.0);
    }
    let ao = pow(clamp(1.0 - occ / 12.0, 0.0, 1.0), ubo.params.y);
    return vec4f(vec3f(mix(1.0, ao, ubo.intensity)), 1.0);
}
)wgsl";

// ---- Voxel rect --------------------------------------------------------------
inline const char *kVoxelRectVertWgsl = R"wgsl(
struct VSIn {
    @location(0) corner: vec2f,
    @location(1) packed: u32,
    @location(2) aoWord: u32,
};
struct PC {
    viewProj: mat4x4f,
    chunkOrigin: vec4f,
    atlasInfo: vec4f,
    tint: vec4f,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) @interpolate(flat) vAO: f32,
};
@group(0) @binding(0) var<uniform> pc: PC;
@vertex
fn vs_main(in: VSIn) -> VSOut {
    var out: VSOut;
    // packed: x(5) y(5) z(5) w(5) h(5) tex(7)
    let px = f32(in.packed & 31u);
    let py = f32((in.packed >> 5u) & 31u);
    let pz = f32((in.packed >> 10u) & 31u);
    let w = f32((in.packed >> 15u) & 31u);
    let h = f32((in.packed >> 20u) & 31u);
    let tex = f32((in.packed >> 25u) & 127u);
    // Per-instance AO word: 2 bits per corner (0..3), Vulkan corner order.
    let ao0 = in.aoWord & 3u;
    let ao1 = (in.aoWord >> 2u) & 3u;
    let ao2 = (in.aoWord >> 4u) & 3u;
    let ao3 = (in.aoWord >> 6u) & 3u;
    let aoTop = select(ao1, ao0, in.corner.x < 0.5);
    let aoBot = select(ao2, ao3, in.corner.x < 0.5);
    let ao = select(aoBot, aoTop, in.corner.y < 0.5);
    var world: vec3f = pc.chunkOrigin.xyz + vec3f(px, py, pz);
    let face = i32(pc.chunkOrigin.w + 0.5);
    var u: f32 = in.corner.x;
    var v: f32 = in.corner.y;
    if (face == 0) { world += vec3f(0.0, v * h, u * w); }
    else if (face == 1) { world += vec3f(0.0, v * h, w - u * w); }
    else if (face == 2) { world += vec3f(u * w, 0.0, v * h); }
    else if (face == 3) { world += vec3f(w - u * w, 0.0, v * h); }
    else if (face == 4) { world += vec3f(u * w, v * h, 0.0); }
    else { world += vec3f(w - u * w, v * h, 0.0); }
    out.pos = pc.viewProj * vec4f(world, 1.0);
    let tiles = max(pc.atlasInfo.x, 1.0);
    let tw = 1.0 / tiles;
    let col = floor(tex / tiles);
    let row = tex - col * tiles;
    out.uv = vec2f((u + row) * tw, (v + col) * tw);
    out.tint = pc.tint;
    out.vAO = f32(ao) / 3.0;
    return out;
}
)wgsl";

inline const char *kVoxelRectFragWgsl = R"wgsl(
struct FSIn {
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) @interpolate(flat) vAO: f32,
};
@group(0) @binding(1) var atlas: texture_2d<f32>;
@group(0) @binding(2) var atlasSamp: sampler;
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    // Vertex ambient occlusion (0..1): darkens corners near other voxels.
    let aoShade = 0.35 + 0.65 * in.vAO;
    return textureSample(atlas, atlasSamp, in.uv) * in.tint * aoShade;
}
)wgsl";

}  // namespace eve::graphics::webgpu
