#include "graphics/HairShader.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "graphics/shaders/HairWgsl.h"

#include <array>
#include <vector>

namespace eve::graphics::hair {
namespace {

const std::array<const char *, 15> kParams = {
    "specExp",           "specStrength",     "primaryShift",       "secondaryShift",
    "alphaCutoff",       "rimStrength",      "strandDirX",         "strandDirY",
    "strandDirZ",        "marschnerR",       "marschnerTT",        "marschnerTRT",
    "selfShadowStrength", "selfShadowBias",  "rootAoStrength"};


std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

}  // namespace

int paramCount() { return int(kParams.size()); }

std::string paramName(int index) {
    if (index < 0 || index >= int(kParams.size())) return {};
    return kParams[size_t(index)];
}

void bindDefaults(Shader *shader) {
    if (!shader) throw eve::Exception("hair::bindDefaults: null shader");
    shader->declareFloat("specExp");
    shader->declareFloat("specStrength");
    shader->declareFloat("primaryShift");
    shader->declareFloat("secondaryShift");
    shader->declareFloat("alphaCutoff");
    shader->declareFloat("rimStrength");
    shader->declareVec3("strandDir");
    shader->declareFloat("marschnerR");
    shader->declareFloat("marschnerTT");
    shader->declareFloat("marschnerTRT");
    shader->declareFloat("selfShadowStrength");
    shader->declareFloat("selfShadowBias");
    shader->declareFloat("rootAoStrength");
    shader->sendFloat("specExp", 80.f);
    shader->sendFloat("specStrength", 0.85f);
    shader->sendFloat("primaryShift", 0.08f);
    shader->sendFloat("secondaryShift", -0.06f);
    shader->sendFloat("alphaCutoff", 0.15f);
    shader->sendFloat("rimStrength", 0.35f);
    shader->sendVec3("strandDir", 0.f, 0.f, 0.f);
    shader->sendFloat("marschnerR", 1.f);
    shader->sendFloat("marschnerTT", 0.45f);
    shader->sendFloat("marschnerTRT", 0.25f);
    // Mild analytical self-shadow on by default; set strength 0 to disable.
    shader->sendFloat("selfShadowStrength", 0.35f);
    shader->sendFloat("selfShadowBias", 0.25f);
    shader->sendFloat("rootAoStrength", 0.3f);
}

Shader *createShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("hair::createShader: null graphics");
    auto vert = copySpv(mesh3d_hair_vert_spv, mesh3d_hair_vert_spv_count);
    auto frag = copySpv(mesh3d_hair_frag_spv, mesh3d_hair_frag_spv_count);
    Shader *sh = gfx->getBackendName() == "webgpu"
                     ? gfx->newHairShaderFromWgsl({}, shaders::kHairFragWgsl)
                     : gfx->newHairShaderFromSpv(vert, frag);
    if (!sh || !sh->gpuHandle)
        throw eve::Exception("hair::createShader: failed to create hair shader");
    bindDefaults(sh);
    return sh;
}

}  // namespace eve::graphics::hair
