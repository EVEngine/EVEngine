#include "graphics/TreeWind.h"
#include <vector>
#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/TreeWindWgsl.h"
#include "graphics/shaders/mesh3d_tree_wind_vert_spv.inc"

namespace eve::graphics {
namespace {
std::vector<uint32_t> embedded(const uint32_t* data, size_t count) { return {data, data + count}; }
void declareLayout(Shader& shader) {
    shader.declareVec4("windGlobals");
    shader.declareVec2("windPhaseDistance");
    shader.declareVec3("windFlex");
    shader.declareVec3("windFrequency");
    shader.declareVec2("windSineTime");
    shader.declareVec2("treeDimensions");
}
}  // namespace

Shader* createTreeWindShader(Graphics* graphics) {
    if (!graphics) throw eve::Exception("tree.wind: null graphics");
    Shader* shader = graphics->getBackendName() == "webgpu"
                         ? graphics->newMeshShaderFromWgsl(shaders::kTreeWindVertWgsl, {})
                         : graphics->newMeshShaderFromSpv(
                               embedded(mesh3d_tree_wind_vert_spv, mesh3d_tree_wind_vert_spv_count), {});
    if (!shader || !shader->gpuHandle) throw eve::Exception("tree.wind: failed to create shader");
    declareLayout(*shader);
    return shader;
}
}  // namespace eve::graphics
