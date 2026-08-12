#include "graphics/HairShader.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"

#include <array>
#include <vector>

namespace eve::graphics::hair {
namespace {

const std::array<const char *, 9> kParams = {"specExp",      "specStrength", "primaryShift",
                                             "secondaryShift", "alphaCutoff",  "rimStrength",
                                             "strandDirX",   "strandDirY",   "strandDirZ"};

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
    shader->sendFloat("specExp", 80.f);
    shader->sendFloat("specStrength", 0.85f);
    shader->sendFloat("primaryShift", 0.08f);
    shader->sendFloat("secondaryShift", -0.06f);
    shader->sendFloat("alphaCutoff", 0.15f);
    shader->sendFloat("rimStrength", 0.35f);
    shader->sendVec3("strandDir", 0.f, 0.f, 0.f);
}

Shader *createShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("hair::createShader: null graphics");
    auto vert = copySpv(mesh3d_hair_vert_spv, mesh3d_hair_vert_spv_count);
    auto frag = copySpv(mesh3d_hair_frag_spv, mesh3d_hair_frag_spv_count);
    Shader *sh = gfx->newHairShaderFromSpv(vert, frag);
    if (!sh || !sh->gpuHandle)
        throw eve::Exception("hair::createShader: failed to create hair shader");
    bindDefaults(sh);
    return sh;
}

}  // namespace eve::graphics::hair
