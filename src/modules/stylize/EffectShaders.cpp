#include "stylize/EffectShaders.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_toon_vert_spv.inc"
#include "stylize/shaders/chromatic_post_frag_spv.inc"
#include "stylize/shaders/dissolve_mesh_frag_spv.inc"
#include "stylize/shaders/grain_post_frag_spv.inc"
#include "stylize/shaders/hologram_mesh_frag_spv.inc"
#include "stylize/shaders/slash_mesh_frag_spv.inc"
#include "stylize/shaders/ember_mesh_frag_spv.inc"
#include "stylize/shaders/aura_mesh_frag_spv.inc"
#include "stylize/shaders/rim_mesh_frag_spv.inc"
#include "stylize/shaders/snow_mesh_frag_spv.inc"
#include "stylize/shaders/vignette_post_frag_spv.inc"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace eve::stylize {
namespace {

// Mesh effect styles (reuse mesh3d_toon.vert; no scene buffers needed).
const StyleDefinition kMeshEffects[] = {
    {"rim", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 40},
    {"dissolve", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 41},
    {"hologram", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 42},
    {"snow", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 43},
    {"slash", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 44},
    {"ember", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 45},
    {"aura", false, true, false, false, false, graphics::PostEffectStage::AfterOpaque, 46},
};

// Post effect styles (full-screen passes, color input only).
const StyleDefinition kPostEffects[] = {
    {"vignette", true, false, false, false, false, graphics::PostEffectStage::AfterTonemap, 260},
    {"chromatic", true, false, false, false, false, graphics::PostEffectStage::AfterTonemap, 270},
    {"grain", true, false, false, false, false, graphics::PostEffectStage::AfterTonemap, 280},
};

const StyleParameterDesc kRimParams[] = {
    {"colorR", 0.4f, 0.f, 2.f},           {"colorG", 0.6f, 0.f, 2.f},      {"colorB", 1.f, 0.f, 2.f},
    {"rimPower", 3.f, 0.1f, 16.f},        {"rimIntensity", 1.f, 0.f, 4.f}, {"rimMix", 1.f, 0.f, 1.f},
    {"ambientStrength", 0.15f, 0.f, 1.f}, {"baseLight", 1.f, 0.f, 3.f},
};

const StyleParameterDesc kDissolveParams[] = {
    {"amount", 0.5f, 0.f, 1.f},      {"edgeWidth", 0.1f, 0.01f, 0.5f}, {"edgeColorR", 1.f, 0.f, 2.f},
    {"edgeColorG", 0.5f, 0.f, 2.f},  {"edgeColorB", 0.1f, 0.f, 2.f},   {"edgeGlow", 2.f, 0.f, 6.f},
    {"noiseScale", 2.f, 0.1f, 16.f}, {"hardness", 0.5f, 0.f, 1.f},
};

const StyleParameterDesc kHologramParams[] = {
    {"colorR", 0.2f, 0.f, 2.f},         {"colorG", 0.9f, 0.f, 2.f},      {"colorB", 1.f, 0.f, 2.f},
    {"fresnelStrength", 1.f, 0.f, 4.f}, {"scanDensity", 8.f, 1.f, 48.f}, {"scanIntensity", 0.5f, 0.f, 1.f},
    {"flicker", 0.3f, 0.f, 1.f},        {"alphaBase", 0.35f, 0.f, 1.f},  {"glitchAmplitude", 0.15f, 0.f, 1.f},
};

const StyleParameterDesc kSnowParams[] = {
    {"snowAmount", 0.9f, 0.f, 1.f},       {"snowHardness", 0.5f, 0.f, 1.f}, {"snowColorR", 0.93f, 0.f, 1.f},
    {"snowColorG", 0.95f, 0.f, 1.f},      {"snowColorB", 0.98f, 0.f, 1.f},  {"noiseScale", 3.f, 0.1f, 16.f},
    {"snowHeight", 0.f, -1000.f, 1000.f}, {"heightFade", 4.f, 0.1f, 100.f},
};

const StyleParameterDesc kSlashParams[] = {
    {"coreR", 0.85f, 0.f, 2.f},          {"coreG", 0.95f, 0.f, 2.f},      {"coreB", 1.0f, 0.f, 2.f},
    {"edgeR", 1.0f, 0.f, 2.f},           {"edgeG", 0.4f, 0.f, 2.f},       {"edgeB", 0.2f, 0.0f, 2.f},
    {"intensity", 1.6f, 0.f, 4.f},       {"width", 0.06f, 0.005f, 0.3f}, {"softness", 0.12f, 0.001f, 0.5f},
    {"noiseScale", 4.f, 0.1f, 20.f},      {"time", 0.f, 0.f, 120.f},      {"speed", 0.9f, 0.f, 3.f},
    {"flowWarp", 0.35f, 0.f, 2.f},        {"edgeDistortion", 0.08f, 0.f, 0.5f},
    {"depthSoftness", 0.004f, 0.0001f, 0.1f}, {"intersectionWidth", 0.006f, 0.0001f, 0.1f},
    {"intersectionGlow", 0.8f, 0.f, 4.f},
    {"refractionStrength", 0.f, 0.f, 1.f},
};

const StyleParameterDesc kEmberParams[] = {
    {"coreR", 1.0f, 0.f, 2.f},        {"coreG", 0.35f, 0.f, 2.f},    {"coreB", 0.08f, 0.f, 2.f},
    {"flareR", 1.0f, 0.f, 2.f},       {"flareG", 0.58f, 0.f, 2.f},   {"flareB", 0.2f, 0.f, 2.f},
    {"burnAmount", 0.68f, 0.f, 1.f},   {"flicker", 0.5f, 0.f, 1.f},   {"noiseScale", 2.5f, 0.1f, 16.f},
    {"hardness", 0.6f, 0.f, 1.f},     {"time", 0.f, 0.f, 120.f},
};

const StyleParameterDesc kAuraParams[] = {
    {"auraR", 0.24f, 0.f, 2.f},        {"auraG", 0.62f, 0.f, 2.f},    {"auraB", 1.f, 0.f, 2.f},
    {"pulse", 1.1f, 0.f, 4.f},          {"edge", 2.f, 0.1f, 8.f},      {"radius", 0.65f, 0.1f, 2.f},
    {"noiseScale", 3.f, 0.1f, 16.f},    {"time", 0.f, 0.f, 120.f},     {"intensity", 1.1f, 0.f, 4.f},
};

const StyleParameterDesc kVignetteParams[] = {
    {"strength", 0.5f, 0.f, 1.f}, {"smoothness", 0.5f, 0.01f, 1.f}, {"roundness", 0.5f, 0.f, 1.f},
    {"colorR", 0.f, 0.f, 1.f},    {"colorG", 0.f, 0.f, 1.f},        {"colorB", 0.f, 0.f, 1.f},
};

const StyleParameterDesc kChromaticParams[] = {
    {"strength", 0.3f, 0.f, 1.f},
    {"radialMix", 1.f, 0.f, 1.f},
    {"rgbSplit", 0.5f, 0.f, 1.f},
};

const StyleParameterDesc kGrainParams[] = {
    {"strength", 0.15f, 0.f, 1.f},
    {"grainSize", 4.f, 1.f, 32.f},
    {"lumaAmount", 0.7f, 0.f, 1.f},
    {"colorAmount", 0.1f, 0.f, 1.f},
};

const StyleDefinition* findMesh(const std::string& style) {
    for (const auto& d : kMeshEffects)
        if (style == d.id) return &d;
    return nullptr;
}
const StyleDefinition* findPost(const std::string& style) {
    for (const auto& d : kPostEffects)
        if (style == d.id) return &d;
    return nullptr;
}

template <size_t N>
const StyleParameterDesc* paramAt(const StyleParameterDesc (&arr)[N], int index) {
    return index < 0 || index >= int(N) ? nullptr : &arr[size_t(index)];
}

int meshParamCount(const std::string& style) {
    if (style == "rim") return int(sizeof(kRimParams) / sizeof(kRimParams[0]));
    if (style == "dissolve") return int(sizeof(kDissolveParams) / sizeof(kDissolveParams[0]));
    if (style == "hologram") return int(sizeof(kHologramParams) / sizeof(kHologramParams[0]));
    if (style == "snow") return int(sizeof(kSnowParams) / sizeof(kSnowParams[0]));
    if (style == "slash") return int(sizeof(kSlashParams) / sizeof(kSlashParams[0]));
    if (style == "ember") return int(sizeof(kEmberParams) / sizeof(kEmberParams[0]));
    if (style == "aura") return int(sizeof(kAuraParams) / sizeof(kAuraParams[0]));
    return 0;
}
const StyleParameterDesc* meshParamAt(const std::string& style, int index) {
    if (style == "rim") return paramAt(kRimParams, index);
    if (style == "dissolve") return paramAt(kDissolveParams, index);
    if (style == "hologram") return paramAt(kHologramParams, index);
    if (style == "snow") return paramAt(kSnowParams, index);
    if (style == "slash") return paramAt(kSlashParams, index);
    if (style == "ember") return paramAt(kEmberParams, index);
    if (style == "aura") return paramAt(kAuraParams, index);
    return nullptr;
}
int postParamCount(const std::string& style) {
    if (style == "vignette") return int(sizeof(kVignetteParams) / sizeof(kVignetteParams[0]));
    if (style == "chromatic") return int(sizeof(kChromaticParams) / sizeof(kChromaticParams[0]));
    if (style == "grain") return int(sizeof(kGrainParams) / sizeof(kGrainParams[0]));
    return 0;
}
const StyleParameterDesc* postParamAt(const std::string& style, int index) {
    if (style == "vignette") return paramAt(kVignetteParams, index);
    if (style == "chromatic") return paramAt(kChromaticParams, index);
    if (style == "grain") return paramAt(kGrainParams, index);
    return nullptr;
}

// The push-constant layout index of a param equals its position in the param
// array, so bind in array order.
void bindParamArray(graphics::Shader* shader, const std::string& style, bool mesh) {
    if (!shader) throw eve::Exception("bindParamArray: null shader");
    int n = mesh ? meshParamCount(style) : postParamCount(style);
    for (int i = 0; i < n; ++i) {
        const StyleParameterDesc* d = mesh ? meshParamAt(style, i) : postParamAt(style, i);
        if (!d) break;
        shader->declareFloat(d->id);
        shader->sendFloat(d->id, d->defaultValue);
    }
}

std::vector<uint32_t> copySpv(const uint32_t* data, size_t count) { return std::vector<uint32_t>(data, data + count); }

}  // namespace

bool isEffectStyle(const std::string& style) { return findMesh(style) != nullptr || findPost(style) != nullptr; }

const StyleDefinition* findEffectDefinition(const std::string& style) {
    if (const StyleDefinition* d = findMesh(style)) return d;
    return findPost(style);
}

int effectStyleCount() {
    return int(sizeof(kMeshEffects) / sizeof(kMeshEffects[0])) + int(sizeof(kPostEffects) / sizeof(kPostEffects[0]));
}

std::string effectStyleIdAt(int index) {
    const int meshCount = int(sizeof(kMeshEffects) / sizeof(kMeshEffects[0]));
    if (index >= 0 && index < meshCount) return kMeshEffects[index].id;
    index -= meshCount;
    if (index >= 0 && index < int(sizeof(kPostEffects) / sizeof(kPostEffects[0]))) return kPostEffects[index].id;
    return {};
}

int effectParamCount(const std::string& style) {
    if (findMesh(style)) return meshParamCount(style);
    return postParamCount(style);
}

std::string effectParamName(const std::string& style, int index) {
    const StyleParameterDesc* d = effectParameterAt(style, index);
    return d ? d->id : std::string{};
}

const StyleParameterDesc* findEffectParameter(const std::string& style, const std::string& name) {
    const int n = effectParamCount(style);
    for (int i = 0; i < n; ++i) {
        const StyleParameterDesc* d = effectParameterAt(style, i);
        if (d && name == d->id) return d;
    }
    return nullptr;
}

const StyleParameterDesc* effectParameterAt(const std::string& style, int index) {
    if (findMesh(style)) return meshParamAt(style, index);
    return postParamAt(style, index);
}

void bindEffectPostUniforms(graphics::Shader* shader, const std::string& style) {
    bindParamArray(shader, style, /*mesh=*/false);
    // Chromatic dispersion needs the framebuffer size to keep the RGB split
    // aspect-correct; StylePass::uploadScreenUniforms refreshes these.
    if (style == "chromatic") {
        shader->declareFloat("screenW");
        shader->declareFloat("screenH");
        shader->sendFloat("screenW", 256.f);
        shader->sendFloat("screenH", 256.f);
    }
}

void bindEffectMeshUniforms(graphics::Shader* shader, const std::string& style) {
    bindParamArray(shader, style, /*mesh=*/true);
}

graphics::Shader* createEffectPostShader(graphics::Graphics* gfx, const std::string& style) {
    if (!gfx) throw eve::Exception("createEffectPostShader: null graphics");
    std::vector<uint32_t> frag;
    if (style == "vignette")
        frag = copySpv(vignette_post_frag_spv, vignette_post_frag_spv_count);
    else if (style == "chromatic")
        frag = copySpv(chromatic_post_frag_spv, chromatic_post_frag_spv_count);
    else if (style == "grain")
        frag = copySpv(grain_post_frag_spv, grain_post_frag_spv_count);
    else
        throw eve::Exception("createEffectPostShader: not an effect post style '%s'", style.c_str());

    graphics::Shader* sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("createEffectPostShader: failed to create '%s'", style.c_str());
    bindEffectPostUniforms(sh, style);
    return sh;
}

graphics::Shader* createEffectMeshShader(graphics::Graphics* gfx, const std::string& style) {
    if (!gfx) throw eve::Exception("createEffectMeshShader: null graphics");
    std::vector<uint32_t> frag;
    if (style == "rim")
        frag = copySpv(rim_mesh_frag_spv, rim_mesh_frag_spv_count);
    else if (style == "dissolve")
        frag = copySpv(dissolve_mesh_frag_spv, dissolve_mesh_frag_spv_count);
    else if (style == "hologram")
        frag = copySpv(hologram_mesh_frag_spv, hologram_mesh_frag_spv_count);
    else if (style == "snow")
        frag = copySpv(snow_mesh_frag_spv, snow_mesh_frag_spv_count);
    else if (style == "slash")
        frag = copySpv(slash_mesh_frag_spv, slash_mesh_frag_spv_count);
    else if (style == "ember")
        frag = copySpv(ember_mesh_frag_spv, ember_mesh_frag_spv_count);
    else if (style == "aura")
        frag = copySpv(aura_mesh_frag_spv, aura_mesh_frag_spv_count);
    else
        throw eve::Exception("createEffectMeshShader: not an effect mesh style '%s'", style.c_str());

    auto              vert = copySpv(mesh3d_toon_vert_spv, mesh3d_toon_vert_spv_count);
    graphics::Shader* sh   = gfx->newMeshShaderFromSpv(vert, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("createEffectMeshShader: failed to create '%s'", style.c_str());
    bindEffectMeshUniforms(sh, style);
    return sh;
}

}  // namespace eve::stylize
