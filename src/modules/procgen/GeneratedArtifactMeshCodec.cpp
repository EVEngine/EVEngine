#include "procgen/GeneratedArtifactMeshCodec.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
namespace eve::procgen::artifact_mesh {
namespace {
template <class T>
eve::Value encodeNumbers(const std::vector<T>& values) {
    eve::Value::Array out;
    out.reserve(values.size());
    for (auto value : values) {
        if constexpr (std::is_integral_v<T>)
            out.emplace_back(static_cast<std::int64_t>(value));
        else
            out.emplace_back(value);
    }
    return eve::Value(std::move(out));
}
eve::Value encodeStringMap(const std::unordered_map<std::string, std::string>& values) {
    eve::Value::Object out;
    for (const auto& [key, value] : values) out.emplace(key, eve::Value(value));
    return eve::Value(std::move(out));
}
}  // namespace
bool validMesh(const MeshBuild& mesh) noexcept {
    if (mesh.empty() || mesh.positions().size() % 3u != 0u || mesh.indices().size() % 3u != 0u) return false;
    const std::size_t vertices = mesh.positions().size() / 3u;
    if ((!mesh.normals().empty() && mesh.normals().size() != mesh.positions().size()) ||
        (!mesh.uvs().empty() && mesh.uvs().size() != vertices * 2u) ||
        (!mesh.colors().empty() && mesh.colors().size() != vertices * 4u))
        return false;
    return std::all_of(mesh.positions().begin(), mesh.positions().end(),
                       [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.normals().begin(), mesh.normals().end(),
                       [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.uvs().begin(), mesh.uvs().end(), [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.colors().begin(), mesh.colors().end(), [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.indices().begin(), mesh.indices().end(),
                       [vertices](std::uint32_t index) { return index < vertices; });
}
eve::Value encodeMesh(const MeshBuild& mesh) {
    eve::Value::Object result;
    result.emplace("positions", encodeNumbers(mesh.positions()));
    result.emplace("normals", encodeNumbers(mesh.normals()));
    result.emplace("uvs", encodeNumbers(mesh.uvs()));
    result.emplace("colors", encodeNumbers(mesh.colors()));
    result.emplace("indices", encodeNumbers(mesh.indices()));
    result.emplace("groupNames", [&mesh] {
        eve::Value::Array names;
        names.reserve(mesh.groupNames().size());
        for (const auto& name : mesh.groupNames()) names.emplace_back(name);
        return eve::Value(std::move(names));
    }());
    result.emplace("triangleGroups", encodeNumbers(mesh.triangleGroups()));
    result.emplace("activeGroup", eve::Value(mesh.activeGroup()));
    result.emplace("metadata", encodeStringMap(mesh.metadata()));
    return eve::Value(std::move(result));
}
Result<std::vector<float>> decodeColors(const eve::Value::Object& object, std::size_t vertices) {
    std::vector<float> colors;
    auto               it = object.find("colors");
    // Dense snapshot v2 predates colors. Missing means the original empty stream.
    if (it == object.end()) return Result<std::vector<float>>::success(std::move(colors));
    auto fail = [] {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid artifact mesh colors", "colors"));
    };
    const auto* array = it->second.getIf<eve::Value::Array>();
    if (!array || (!array->empty() && array->size() != vertices * 4u)) return fail();
    colors.reserve(array->size());
    for (const auto& item : *array) {
        double value;
        if (const auto* number = item.getIf<double>())
            value = *number;
        else if (const auto* integer = item.getIf<std::int64_t>())
            value = static_cast<double>(*integer);
        else
            return fail();
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return fail();
        colors.push_back(static_cast<float>(value));
    }
    return Result<std::vector<float>>::success(std::move(colors));
}
}  // namespace eve::procgen::artifact_mesh
