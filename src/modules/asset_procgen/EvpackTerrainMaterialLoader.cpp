#include "asset_procgen/EvpackTerrainMaterialLoader.h"

#include "asset/RuntimeDefinition.h"
#include "common/Utf8Validation.h"

#include <cmath>

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.procgen.terrain-material"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::string> textField(const Value::Object& object, std::string_view name,
                              const TerrainMaterialLoadLimits& limits, bool allowEmpty) {
    const Value* value = field(object, name);
    if (!value || !value->isString() || (!allowEmpty && value->asString().empty()) ||
        value->asString().size() > limits.maximumStringBytes ||
        !isValidUtf8(value->asString(), Utf8NullPolicy::Reject))
        return failure<std::string>(DiagnosticCode::ParseError,
                                    "terrain layer string is invalid", std::string(name));
    return Result<std::string>::success(value->asString());
}

}  // namespace

Result<LoadedTerrainMaterial> EvpackTerrainMaterialLoader::load(
    const AssetRef& material, const asset::EvpackCapabilities& capabilities,
    const TerrainMaterialLoadLimits& limits) const {
    auto payload = reader_.read(material, "eve.terrain-material/1", capabilities,
                                limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedTerrainMaterial>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Definition)
            return failure<LoadedTerrainMaterial>(DiagnosticCode::TypeMismatch,
                                                  "terrain material cannot contain runtime bulk");
        if (definition)
            return failure<LoadedTerrainMaterial>(DiagnosticCode::Conflict,
                                                  "terrain material has duplicate definitions");
        definition = &chunk;
    }
    if (!definition)
        return failure<LoadedTerrainMaterial>(DiagnosticCode::NotFound,
                                              "terrain material definition is missing");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes = limits.maximumDecodedBytes;
    definitionLimits.maximumStringBytes = limits.maximumStringBytes;
    auto metadata = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedTerrainMaterial>::failure(metadata.status());
    const auto* root = metadata.value().getIf<Value::Object>();
    const Value* schema = root ? field(*root, "schema") : nullptr;
    const Value* version = root ? field(*root, "schemaVersion") : nullptr;
    const Value* layersValue = root ? field(*root, "layers") : nullptr;
    const auto* layers = layersValue ? layersValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.terrain-material" ||
        !version || !version->isInt64() || version->asInt() != 1 || !layers ||
        layers->empty() || layers->size() > limits.maximumLayers)
        return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                              "terrain material envelope is invalid");
    std::vector<RuntimeTerrainLayer> decoded;
    decoded.reserve(layers->size());
    for (std::size_t index = 0; index < layers->size(); ++index) {
        const auto* layer = (*layers)[index].getIf<Value::Object>();
        if (!layer)
            return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                                  "terrain layer must be an object",
                                                  std::to_string(index));
        auto name = textField(*layer, "name", limits, false);
        auto diffuse = textField(*layer, "diffuseSource", limits, true);
        auto normal = textField(*layer, "normalSource", limits, true);
        auto weight = textField(*layer, "weightSource", limits, true);
        auto convention = textField(*layer, "normalConvention", limits, false);
        const Value* tile = field(*layer, "tileSizeMeters");
        if (!name || !diffuse || !normal || !weight || !convention || !tile ||
            (!tile->isDouble() && !tile->isInt64()))
            return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                                  "terrain layer fields are incomplete",
                                                  std::to_string(index));
        const double tileSize = tile->isDouble() ? tile->asDouble() : double(tile->asInt());
        if (!std::isfinite(tileSize) || tileSize <= 0 || tileSize > 1'000'000.0 ||
            (convention.value() != "opengl" && convention.value() != "directx"))
            return failure<LoadedTerrainMaterial>(DiagnosticCode::InvalidArgument,
                                                  "terrain layer physical semantics are invalid",
                                                  std::to_string(index));
        decoded.push_back({std::move(name).takeValue(), std::move(diffuse).takeValue(),
                           std::move(normal).takeValue(), std::move(weight).takeValue(),
                           std::move(convention).takeValue(), static_cast<float>(tileSize)});
    }
    return Result<LoadedTerrainMaterial>::success(
        {material, std::move(decoded), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_procgen
