#include "procgen/heightmap_target/TerrainDocumentCodec.h"

#include "editing/EditingResult.h"

#include <cmath>
#include <limits>
#include <set>
#include <string>

namespace eve::heightmap_target {
namespace {

template <class T>
editor::EditorResult<T> rejected(std::string message) {
    return editing::failed<T>(editor::EditorStatus::Rejected, editor::RuleId("editor.terrain.document-invalid"),
                              std::move(message));
}

const editor::EditorValue* field(const editor::EditorValue::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool dimension(const editor::EditorValue* value, std::uint32_t& out) {
    const auto* integer = value ? value->getIf<std::int64_t>() : nullptr;
    if (!integer || *integer <= 0 || std::uint64_t(*integer) > std::numeric_limits<std::uint32_t>::max()) return false;
    out = static_cast<std::uint32_t>(*integer);
    return true;
}

bool finiteNumber(const editor::EditorValue* value, float& out) {
    if (!value) return false;
    double number = 0.0;
    if (const auto* integer = value->getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else if (const auto* real = value->getIf<double>())
        number = *real;
    else
        return false;
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        return false;
    out = static_cast<float>(number);
    return true;
}

}  // namespace

editor::EditorResult<editor::EditorValue> encodeTerrainDocument(const procgen::Heightmap& heightmap, float spacingX,
                                                                float spacingZ, const TerrainDocumentLimits& limits) {
    const std::uint64_t width   = static_cast<std::uint64_t>(heightmap.getWidth());
    const std::uint64_t height  = static_cast<std::uint64_t>(heightmap.getHeight());
    const std::uint64_t samples = width * height;
    if (width == 0 || height == 0 || width > limits.maximumDimension || height > limits.maximumDimension ||
        samples > limits.maximumSamples || !std::isfinite(spacingX) || !std::isfinite(spacingZ) || spacingX <= 0.0F ||
        spacingZ <= 0.0F || samples != heightmap.data().size())
        return rejected<editor::EditorValue>("Terrain dimensions, spacing, or sample count is invalid");
    editor::EditorValue::Array heights;
    heights.reserve(heightmap.data().size());
    for (float value : heightmap.data()) {
        if (!std::isfinite(value)) return rejected<editor::EditorValue>("Terrain contains a non-finite height");
        heights.emplace_back(static_cast<double>(value));
    }
    return editing::applied<editor::EditorValue>(
        editor::EditorValue::Object{{"schema", "eve.terrain-editor-document"},
                                    {"schemaVersion", std::int64_t{1}},
                                    {"width", static_cast<std::int64_t>(width)},
                                    {"height", static_cast<std::int64_t>(height)},
                                    {"spacingX", static_cast<double>(spacingX)},
                                    {"spacingZ", static_cast<double>(spacingZ)},
                                    {"heights", std::move(heights)}});
}

editor::EditorResult<TerrainDocumentData> decodeTerrainDocument(const editor::EditorValue&   value,
                                                                const TerrainDocumentLimits& limits) {
    const auto*                                     object = value.getIf<editor::EditorValue::Object>();
    static const std::set<std::string, std::less<>> allowed{"schema",   "schemaVersion", "width",  "height",
                                                            "spacingX", "spacingZ",      "heights"};
    if (!object || object->size() != allowed.size())
        return rejected<TerrainDocumentData>("Terrain document fields do not match schema version 1");
    for (const auto& [name, ignored] : *object) {
        (void)ignored;
        if (!allowed.contains(name))
            return rejected<TerrainDocumentData>("Terrain document contains an unknown field: " + name);
    }
    const auto* schema        = field(*object, "schema");
    const auto* version       = field(*object, "schemaVersion");
    const auto* schemaText    = schema ? schema->getIf<std::string>() : nullptr;
    const auto* versionNumber = version ? version->getIf<std::int64_t>() : nullptr;
    if (!schemaText || *schemaText != "eve.terrain-editor-document" || !versionNumber || *versionNumber != 1)
        return rejected<TerrainDocumentData>("Terrain document schema or version is unsupported");
    std::uint32_t width = 0, height = 0;
    float         spacingX = 0.0F, spacingZ = 0.0F;
    if (!dimension(field(*object, "width"), width) || !dimension(field(*object, "height"), height) ||
        width > limits.maximumDimension || height > limits.maximumDimension ||
        std::uint64_t(width) * height > limits.maximumSamples || !finiteNumber(field(*object, "spacingX"), spacingX) ||
        !finiteNumber(field(*object, "spacingZ"), spacingZ) || spacingX <= 0.0F || spacingZ <= 0.0F)
        return rejected<TerrainDocumentData>("Terrain document dimensions or spacing is invalid");
    const auto* heightsValue = field(*object, "heights");
    const auto* heights      = heightsValue ? heightsValue->getIf<editor::EditorValue::Array>() : nullptr;
    if (!heights || heights->size() != std::uint64_t(width) * height)
        return rejected<TerrainDocumentData>("Terrain document height count does not match dimensions");
    TerrainDocumentData result;
    result.heightmap.resize(static_cast<int>(width), static_cast<int>(height));
    result.spacingX = spacingX;
    result.spacingZ = spacingZ;
    for (std::size_t index = 0; index < heights->size(); ++index) {
        float sample = 0.0F;
        if (!finiteNumber(&(*heights)[index], sample))
            return rejected<TerrainDocumentData>("Terrain document contains a non-finite height");
        result.heightmap.data()[index] = sample;
    }
    return editing::applied<TerrainDocumentData>(std::move(result));
}

}  // namespace eve::heightmap_target
