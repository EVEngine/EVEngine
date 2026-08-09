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

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace eve::stylize {
namespace {

const std::array<const char *, 4> kStyles = {"cartoon", "watercolor", "ink", "pixel"};

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

}  // namespace

bool isKnownStyle(const std::string &style) {
    return std::find(kStyles.begin(), kStyles.end(), style) != kStyles.end();
}

int styleCount() { return int(kStyles.size()); }

std::string styleIdAt(int index) {
    if (index < 0 || index >= int(kStyles.size())) return {};
    return kStyles[size_t(index)];
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
        shader->sendFloat("bands", 4.f);
        shader->sendFloat("outlineStrength", 1.0f);
        shader->sendFloat("outlineThreshold", 0.35f);
        shader->sendFloat("posterize", 6.f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("softEdge", 0.08f);
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
        shader->sendFloat("blurAmount", 1.8f);
        shader->sendFloat("edgeDarken", 1.4f);
        shader->sendFloat("paperStrength", 0.55f);
        shader->sendFloat("distortion", 1.2f);
        shader->sendFloat("bleed", 0.65f);
        shader->sendFloat("saturation", 0.85f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("granulation", 0.45f);
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
        shader->sendFloat("edgeThreshold", 0.28f);
        shader->sendFloat("diffusion", 2.5f);
        shader->sendFloat("paperR", 0.93f);
        shader->sendFloat("paperG", 0.90f);
        shader->sendFloat("paperB", 0.82f);
        shader->sendFloat("inkDensity", 0.85f);
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
        shader->sendFloat("pixelSize", 4.f);
        shader->sendFloat("paletteSteps", 8.f);
        shader->sendFloat("ditherStrength", 0.35f);
        shader->sendFloat("toonBands", 4.f);
        shader->sendFloat("sharpness", 1.f);
        shader->sendFloat("texelW", 1.f / 256.f);
        shader->sendFloat("texelH", 1.f / 256.f);
        shader->sendFloat("time", 0.f);
        shader->sendFloat("screenW", 256.f);
        shader->sendFloat("screenH", 256.f);
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
    throw eve::Exception("bindMeshUniforms: style '%s' has no mesh shader", style.c_str());
}

graphics::Shader *createPostShader(graphics::Graphics *gfx, const std::string &style) {
    if (!gfx) throw eve::Exception("createPostShader: null graphics");
    if (!isKnownStyle(style))
        throw eve::Exception("createPostShader: unknown style '%s'", style.c_str());

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
    throw eve::Exception(
        "createMeshShader: style '%s' has no mesh variant (use watercolor/pixel as post)",
        style.c_str());
}

}  // namespace eve::stylize
