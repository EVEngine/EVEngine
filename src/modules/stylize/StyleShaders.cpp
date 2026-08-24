#include "stylize/StyleShaders.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_toon_frag_spv.inc"
#include "graphics/shaders/mesh3d_toon_vert_spv.inc"
#include "stylize/shaders/cartoon_post_frag_spv.inc"
#include "stylize/shaders/ink_mesh_frag_spv.inc"
#include "stylize/shaders/ink_post_frag_spv.inc"
#include "stylize/shaders/pixel_post_frag_spv.inc"
#include "stylize/shaders/watercolor_post_frag_spv.inc"
#include "stylize/shaders/xray_mesh_frag_spv.inc"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace eve::stylize {
namespace {

const std::array<StyleDefinition, 5> kStyles = {{
    {"cartoon", true, true, true, true, true},
    {"watercolor", true, false, true, false, false},
    {"ink", true, true, true, true, true},
    {"pixel", true, false, true, false, false},
    {"xray", false, true, false, true, false},
}};

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

// WebGPU (WGSL) X-ray mesh shaders. Must match the engine's mesh3d Frame UBO
// (group 0 binding 0, std140 = Mesh3DUBO) and shared bindings (main sampler 7,
// scene depth 9). X-ray params are packed into texBomb/parallax/clipInfo by the
// backend flush (see bindMeshUniforms("xray") ordering below).
const char *kXrayMeshVertWgsl = R"wgsl(
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
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
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
    let nrm = transpose(inverse3x3(mat3x3f(ubo.model[0].xyz, ubo.model[1].xyz, ubo.model[2].xyz))) * in.normal;
    out.vNormal = normalize(nrm);
    out.vUV = in.uv;
    out.vTint = ubo.tint;
    out.vCameraPos = ubo.cameraPos.xyz;
    return out;
}
)wgsl";

const char *kXrayMeshFragWgsl = R"wgsl(
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
};
struct FSIn {
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
};
@group(0) @binding(0) var<uniform> ubo: Frame;
@group(0) @binding(7) var mainSamp: sampler;
@group(0) @binding(9) var sceneDepth: texture_depth_2d;

struct FSOut {
    @location(0) color: vec4f,
};

@fragment
fn fs_main(in: FSIn, @builtin(position) fragPos: vec4f) -> FSOut {
    let color = ubo.texBomb.xyz;
    let alpha = clamp(ubo.texBomb.w, 0.0, 1.0);
    let bias = max(ubo.parallax.x, 0.0);
    let screenW = max(ubo.parallax.y, 1.0);
    let screenH = max(ubo.parallax.z, 1.0);
    let rimStrength = clamp(ubo.parallax.w, 0.0, 1.0);
    let rimPower = max(ubo.clipInfo.z, 0.1);

    let uv = fragPos.xy / vec2f(screenW, screenH);
    let sceneZ = textureSampleLevel(sceneDepth, mainSamp, uv, 0.0);
    let charZ = fragPos.z;
    let occ = smoothstep(sceneZ, sceneZ + bias, charZ);
    if (occ < 0.5) {
        discard;
    }
    let N = normalize(in.vNormal);
    let V = normalize(in.vCameraPos - in.vWorldPos);
    let rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), rimPower);
    let edge = mix(1.0, rim, rimStrength);
    var out: FSOut;
    out.color = vec4f(color, alpha * edge * occ);
    return out;
}
)wgsl";

}  // namespace

const StyleDefinition *findStyleDefinition(const std::string &style) {
    const auto it = std::find_if(kStyles.begin(), kStyles.end(), [&](const StyleDefinition &def) {
        return style == def.id;
    });
    return it == kStyles.end() ? nullptr : &*it;
}

bool isKnownStyle(const std::string &style) { return findStyleDefinition(style) != nullptr; }

int styleCount() { return int(kStyles.size()); }

std::string styleIdAt(int index) {
    if (index < 0 || index >= int(kStyles.size())) return {};
    return kStyles[size_t(index)].id;
}

bool styleSupports(const std::string &style, const std::string &feature) {
    const StyleDefinition *def = findStyleDefinition(style);
    if (!def) return false;
    if (feature == "post") return def->post;
    if (feature == "mesh") return def->mesh;
    if (feature == "cpu") return def->cpu;
    if (feature == "depth") return def->depth;
    if (feature == "normal") return def->normal;
    if (feature == "gbuffer") return def->depth || def->normal;
    return false;
}

namespace {
const StyleParameterDesc kCartoonParams[] = {
    {"bands", 3.f, 1.f, 16.f},              {"outlineStrength", 1.15f, 0.f, 4.f},
    {"outlineThreshold", 0.12f, 0.f, 1.f},  {"posterize", 5.f, 1.f, 32.f},
    {"softEdge", 0.08f, 0.f, 1.f},          {"outlineWidth", 1.5f, 0.5f, 8.f},
    {"shadowLift", 0.12f, 0.f, 1.f},        {"rimPower", 2.8f, 0.1f, 16.f},
    {"rimStrength", 0.55f, 0.f, 2.f},
};
const StyleParameterDesc kWatercolorParams[] = {
    {"blurAmount", 2.4f, 0.f, 12.f},      {"edgeDarken", 2.f, 0.f, 5.f},
    {"paperStrength", 0.65f, 0.f, 1.f},   {"distortion", 0.85f, 0.f, 3.f},
    {"bleed", 0.62f, 0.f, 1.f},           {"saturation", 0.9f, 0.f, 2.f},
    {"granulation", 0.65f, 0.f, 1.f},
};
const StyleParameterDesc kInkParams[] = {
    {"inkContrast", 1.35f, 0.f, 4.f},    {"washLevels", 5.f, 1.f, 16.f},
    {"edgeThreshold", 0.18f, 0.f, 1.f}, {"diffusion", 3.5f, 0.f, 12.f},
    {"paperR", 0.96f, 0.f, 1.f},         {"paperG", 0.93f, 0.f, 1.f},
    {"paperB", 0.86f, 0.f, 1.f},         {"inkDensity", 0.75f, 0.f, 2.f},
    {"edgeStrength", 1.1f, 0.f, 4.f},    {"contrast", 1.25f, 0.f, 4.f},
    {"rimBoost", 0.65f, 0.f, 2.f},
};
const StyleParameterDesc kPixelParams[] = {
    {"pixelSize", 5.f, 1.f, 64.f},         {"paletteSteps", 6.f, 2.f, 32.f},
    {"ditherStrength", 0.18f, 0.f, 1.f},  {"toonBands", 3.f, 1.f, 16.f},
    {"sharpness", 1.f, 0.f, 2.f},          {"outline", 0.9f, 0.f, 4.f},
};
const StyleParameterDesc kXrayParams[] = {
    {"colorR", 1.f, 0.f, 1.f},          {"colorG", 0.62f, 0.f, 1.f},
    {"colorB", 0.12f, 0.f, 1.f},       {"bias", 0.0002f, 0.f, 0.02f},
    {"rimPower", 2.2f, 0.1f, 16.f},    {"rimStrength", 0.7f, 0.f, 2.f},
    {"alpha", 0.82f, 0.f, 1.f},
};

template <size_t N>
const StyleParameterDesc *paramAt(const StyleParameterDesc (&params)[N], int index) {
    return index < 0 || index >= int(N) ? nullptr : &params[size_t(index)];
}
}  // namespace

int styleParamCount(const std::string &style) {
    if (style == "cartoon") return int(sizeof(kCartoonParams) / sizeof(kCartoonParams[0]));
    if (style == "watercolor") return int(sizeof(kWatercolorParams) / sizeof(kWatercolorParams[0]));
    if (style == "ink") return int(sizeof(kInkParams) / sizeof(kInkParams[0]));
    if (style == "pixel") return int(sizeof(kPixelParams) / sizeof(kPixelParams[0]));
    if (style == "xray") return int(sizeof(kXrayParams) / sizeof(kXrayParams[0]));
    return 0;
}

std::string styleParamName(const std::string &style, int index) {
    const StyleParameterDesc *desc = styleParameterAt(style, index);
    return desc ? desc->id : std::string{};
}

const StyleParameterDesc *styleParameterAt(const std::string &style, int index) {
    if (style == "cartoon") return paramAt(kCartoonParams, index);
    if (style == "watercolor") return paramAt(kWatercolorParams, index);
    if (style == "ink") return paramAt(kInkParams, index);
    if (style == "pixel") return paramAt(kPixelParams, index);
    if (style == "xray") return paramAt(kXrayParams, index);
    return nullptr;
}

const StyleParameterDesc *findStyleParameter(const std::string &style, const std::string &name) {
    const int count = styleParamCount(style);
    for (int i = 0; i < count; ++i) {
        const StyleParameterDesc *desc = styleParameterAt(style, i);
        if (desc && name == desc->id) return desc;
    }
    return nullptr;
}

void bindPostUniforms(graphics::Shader *shader, const std::string &style) {
    if (!shader) throw eve::Exception("bindPostUniforms: null shader");

    if (style == "cartoon") {
        shader->declareFloat("bands");
        shader->declareFloat("outlineStrength");
        shader->declareFloat("outlineThreshold");
        shader->declareFloat("posterize");
        shader->declareFloat("texelW");
        shader->declareFloat("texelH");
        shader->declareFloat("time");
        shader->declareFloat("softEdge");
        shader->declareFloat("outlineWidth");
        shader->declareFloat("shadowLift");
        shader->sendFloat("bands", 3.f);
        shader->sendFloat("outlineStrength", 1.15f);
        shader->sendFloat("outlineThreshold", 0.12f);
        shader->sendFloat("posterize", 5.f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("softEdge", 0.08f);
        shader->sendFloat("outlineWidth", 1.5f);
        shader->sendFloat("shadowLift", 0.12f);
        return;
    }
    if (style == "watercolor") {
        shader->declareFloat("blurAmount");
        shader->declareFloat("edgeDarken");
        shader->declareFloat("paperStrength");
        shader->declareFloat("distortion");
        shader->declareFloat("bleed");
        shader->declareFloat("saturation");
        shader->declareFloat("texelW");
        shader->declareFloat("texelH");
        shader->declareFloat("time");
        shader->declareFloat("granulation");
        shader->sendFloat("blurAmount", 2.4f);
        shader->sendFloat("edgeDarken", 2.0f);
        shader->sendFloat("paperStrength", 0.65f);
        shader->sendFloat("distortion", 0.85f);
        shader->sendFloat("bleed", 0.62f);
        shader->sendFloat("saturation", 0.9f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("granulation", 0.65f);
        return;
    }
    if (style == "ink") {
        shader->declareFloat("inkContrast");
        shader->declareFloat("washLevels");
        shader->declareFloat("edgeThreshold");
        shader->declareFloat("diffusion");
        shader->declareFloat("paperR");
        shader->declareFloat("paperG");
        shader->declareFloat("paperB");
        shader->declareFloat("inkDensity");
        shader->declareFloat("texelW");
        shader->declareFloat("texelH");
        shader->declareFloat("time");
        shader->declareFloat("edgeStrength");
        shader->sendFloat("inkContrast", 1.35f);
        shader->sendFloat("washLevels", 5.f);
        shader->sendFloat("edgeThreshold", 0.18f);
        shader->sendFloat("diffusion", 3.5f);
        shader->sendFloat("paperR", 0.96f);
        shader->sendFloat("paperG", 0.93f);
        shader->sendFloat("paperB", 0.86f);
        shader->sendFloat("inkDensity", 0.75f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("edgeStrength", 1.1f);
        return;
    }
    if (style == "pixel") {
        shader->declareFloat("pixelSize");
        shader->declareFloat("paletteSteps");
        shader->declareFloat("ditherStrength");
        shader->declareFloat("toonBands");
        shader->declareFloat("sharpness");
        shader->declareFloat("texelW");
        shader->declareFloat("texelH");
        shader->declareFloat("time");
        shader->declareFloat("screenW");
        shader->declareFloat("screenH");
        shader->declareFloat("outline");
        shader->sendFloat("pixelSize", 5.f);
        shader->sendFloat("paletteSteps", 6.f);
        shader->sendFloat("ditherStrength", 0.18f);
        shader->sendFloat("toonBands", 3.f);
        shader->sendFloat("sharpness", 1.f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("screenW", 256.f);
        shader->sendFloat("screenH", 256.f);
        shader->sendFloat("outline", 0.9f);
        return;
    }
    throw eve::Exception("bindPostUniforms: unknown style '%s'", style.c_str());
}

void bindMeshUniforms(graphics::Shader *shader, const std::string &style) {
    if (!shader) throw eve::Exception("bindMeshUniforms: null shader");

    if (style == "cartoon") {
        shader->declareFloat("bands");
        shader->declareFloat("rimPower");
        shader->declareFloat("rimStrength");
        shader->declareFloat("posterize");
        shader->sendFloat("bands", 4.f);
        shader->sendFloat("rimPower", 2.8f);
        shader->sendFloat("rimStrength", 0.55f);
        shader->sendFloat("posterize", 6.f);
        return;
    }
    if (style == "ink") {
        shader->declareFloat("washLevels");
        shader->declareFloat("edgeThreshold");
        shader->declareFloat("inkDensity");
        shader->declareFloat("contrast");
        shader->declareFloat("paperR");
        shader->declareFloat("paperG");
        shader->declareFloat("paperB");
        shader->declareFloat("rimBoost");
        shader->sendFloat("washLevels", 5.f);
        shader->sendFloat("edgeThreshold", 0.35f);
        shader->sendFloat("inkDensity", 0.9f);
        shader->sendFloat("contrast", 1.25f);
        shader->sendFloat("paperR", 0.93f);
        shader->sendFloat("paperG", 0.90f);
        shader->sendFloat("paperB", 0.82f);
        shader->sendFloat("rimBoost", 0.65f);
        return;
    }
    if (style == "xray") {
        shader->declareFloat("colorR");
        shader->declareFloat("colorG");
        shader->declareFloat("colorB");
        shader->declareFloat("bias");
        shader->declareFloat("screenW");
        shader->declareFloat("screenH");
        shader->declareFloat("rimPower");
        shader->declareFloat("rimStrength");
        shader->declareFloat("alpha");
        shader->sendFloat("colorR", 1.f);
        shader->sendFloat("colorG", 0.62f);
        shader->sendFloat("colorB", 0.12f);
        shader->sendFloat("bias", 0.0002f);
        shader->sendFloat("screenW", 256.f);
        shader->sendFloat("screenH", 256.f);
        shader->sendFloat("rimPower", 2.2f);
        shader->sendFloat("rimStrength", 0.7f);
        shader->sendFloat("alpha", 0.82f);
        return;
    }
    throw eve::Exception("bindMeshUniforms: style '%s' has no mesh shader", style.c_str());
}

graphics::Shader *createPostShader(graphics::Graphics *gfx, const std::string &style) {
    if (!gfx) throw eve::Exception("createPostShader: null graphics");
    const StyleDefinition *def = findStyleDefinition(style);
    if (!def) throw eve::Exception("createPostShader: unknown style '%s'", style.c_str());
    if (!def->post)
        throw eve::Exception("createPostShader: style '%s' has no post technique", style.c_str());

    std::vector<uint32_t> frag;
    if (style == "cartoon")
        frag = copySpv(cartoon_post_frag_spv, cartoon_post_frag_spv_count);
    else if (style == "watercolor")
        frag = copySpv(watercolor_post_frag_spv, watercolor_post_frag_spv_count);
    else if (style == "ink")
        frag = copySpv(ink_post_frag_spv, ink_post_frag_spv_count);
    else
        frag = copySpv(pixel_post_frag_spv, pixel_post_frag_spv_count);

    graphics::Shader *sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle)
        throw eve::Exception("createPostShader: failed to create '%s'", style.c_str());
    bindPostUniforms(sh, style);
    return sh;
}

graphics::Shader *createMeshShader(graphics::Graphics *gfx, const std::string &style) {
    if (!gfx) throw eve::Exception("createMeshShader: null graphics");

    if (style == "cartoon") {
        auto vert = copySpv(mesh3d_toon_vert_spv, mesh3d_toon_vert_spv_count);
        auto frag = copySpv(mesh3d_toon_frag_spv, mesh3d_toon_frag_spv_count);
        graphics::Shader *sh = gfx->newMeshShaderFromSpv(vert, frag);
        if (!sh || !sh->gpuHandle)
            throw eve::Exception("createMeshShader: failed to create cartoon mesh shader");
        bindMeshUniforms(sh, style);
        return sh;
    }
    if (style == "ink") {
        auto vert = copySpv(mesh3d_toon_vert_spv, mesh3d_toon_vert_spv_count);
        auto frag = copySpv(ink_mesh_frag_spv, ink_mesh_frag_spv_count);
        graphics::Shader *sh = gfx->newMeshShaderFromSpv(vert, frag);
        if (!sh || !sh->gpuHandle)
            throw eve::Exception("createMeshShader: failed to create ink mesh shader");
        bindMeshUniforms(sh, style);
        return sh;
    }
    if (style == "xray") {
        // WebGPU has no SPIR-V mesh shaders (WGSL only); Vulkan uses SPIR-V.
        if (gfx->getBackendName() == "webgpu") {
            graphics::Shader *sh = gfx->newMeshShaderFromWgsl(kXrayMeshVertWgsl, kXrayMeshFragWgsl);
            if (!sh || !sh->gpuHandle)
                throw eve::Exception("createMeshShader: failed to create xray mesh shader (wgsl)");
            sh->setXray(true);
            bindMeshUniforms(sh, style);
            return sh;
        }
        auto vert = copySpv(mesh3d_toon_vert_spv, mesh3d_toon_vert_spv_count);
        auto frag = copySpv(xray_mesh_frag_spv, xray_mesh_frag_spv_count);
        graphics::Shader *sh = gfx->newMeshShaderFromSpv(vert, frag);
        if (!sh || !sh->gpuHandle)
            throw eve::Exception("createMeshShader: failed to create xray mesh shader");
        sh->setXray(true);
        bindMeshUniforms(sh, style);
        return sh;
    }
    throw eve::Exception(
        "createMeshShader: style '%s' has no mesh variant (use watercolor/pixel as post)",
        style.c_str());
}

}  // namespace eve::stylize
