#include "graphics/VegetationPacking.h"
#include <cmath>
#include <new>
#include "graphics/VegetationMotion.h"

namespace eve::graphics {
namespace {
bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool finite(glm::vec4 value) { return finite(glm::vec3(value)) && std::isfinite(value.w); }
bool packed(float value) {
    return std::isfinite(value) && value >= 0 && value <= 4194303.f && value == std::floor(value);
}
glm::vec2 unpack(float value) { return {std::floor(value / 2048.f) / 2047.f, std::fmod(value, 2048.f) / 2047.f}; }
}  // namespace
Result<std::vector<VegetationVertex>> decodeTveVegetationVertices(std::span<const TvePackedVertex> vertices) {
    if (vertices.size() > 4 * 1024 * 1024)
        return Result<std::vector<VegetationVertex>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation vertex budget exceeded"));
    for (const auto& v : vertices) {
        bool masks = true;
        for (int c = 0; c < 4; ++c) masks = masks && v.color[c] >= 0 && v.color[c] <= 1;
        if (!finite(v.position) || !finite(v.normal) || !finite(v.color) || !finite(v.texcoord0) ||
            !finite(v.texcoord1) || !finite(v.texcoord3) || !packed(v.texcoord0.z) || !packed(v.texcoord0.w) || !masks)
            return Result<std::vector<VegetationVertex>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid TVE vertex attribute or packed pair", {},
                                  {}, "graphics.vegetation.packing"));
    }
    try {
        std::vector<VegetationVertex> result;
        result.reserve(vertices.size());
        for (const auto& source : vertices) {
            VegetationVertex v;
            v.position        = source.position;
            v.normal          = source.normal;
            v.variation       = source.color.r;
            v.occlusion       = source.color.g;
            v.detail          = source.color.b;
            v.bending         = source.color.a;
            const auto motion = unpack(source.texcoord0.z), bounds = unpack(source.texcoord0.w) * 100.f;
            v.branch            = motion.x;
            v.flutter           = motion.y;
            v.boundsHeight      = bounds.x;
            v.boundsRadius      = bounds.y;
            v.texcoord          = glm::vec2(source.texcoord0);
            v.secondaryTexcoord = glm::vec2(source.texcoord1);
            v.detailCoord       = {source.texcoord1.z, source.texcoord1.w};
            v.pivot             = {source.texcoord3.x, source.texcoord3.z, source.texcoord3.y};
            result.push_back(v);
        }
        return Result<std::vector<VegetationVertex>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::vector<VegetationVertex>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation unpack allocation failed"));
    }
}
}  // namespace eve::graphics
