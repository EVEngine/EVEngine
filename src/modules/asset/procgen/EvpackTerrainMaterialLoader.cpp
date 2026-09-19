#include "asset/procgen/EvpackTerrainMaterialLoader.h"

#include "asset/RuntimeDefinition.h"
#include "common/Utf8Validation.h"

#include <cmath>
#include <limits>

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.procgen.terrain-material"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::string> textField(const Value::Object& object, std::string_view name,
                              const TerrainMaterialLoadLimits& limits, bool allowEmpty) {
    const Value* value = field(object, name);
    if (!value || !value->isString() || (!allowEmpty && value->asString().empty()) ||
        value->asString().size() > limits.maximumStringBytes || !isValidUtf8(value->asString(), Utf8NullPolicy::Reject))
        return failure<std::string>(DiagnosticCode::ParseError, "terrain layer string is invalid", std::string(name));
    return Result<std::string>::success(value->asString());
}

Result<std::optional<AssetRef>> assetField(const Value::Object& object, std::string_view name,
                                           const TerrainMaterialLoadLimits& limits) {
    auto text = textField(object, name, limits, true);
    if (!text) return Result<std::optional<AssetRef>>::failure(text.status());
    if (text.value().empty()) return Result<std::optional<AssetRef>>::success(std::nullopt);
    auto parsed = AssetRef::parse(text.value());
    if (!parsed)
        return failure<std::optional<AssetRef>>(DiagnosticCode::ParseError, "terrain image AssetRef is invalid",
                                                std::string(name));
    return Result<std::optional<AssetRef>>::success(std::move(parsed).takeValue());
}

Result<float> numberField(const Value::Object& object, std::string_view name, float fallback) {
    const auto* value = field(object, name);
    if (!value) return Result<float>::success(fallback);
    if (!value->isNumeric())
        return failure<float>(DiagnosticCode::ParseError, "terrain number is invalid", std::string(name));
    const double number = value->isInt64() ? double(value->asInt()) : value->asDouble();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
        return failure<float>(DiagnosticCode::InvalidArgument, "terrain number is outside float range",
                              std::string(name));
    return Result<float>::success(float(number));
}

template <std::size_t N>
Result<std::array<float, N>> vectorField(const Value::Object& object, std::string_view name,
                                         std::array<float, N> fallback) {
    const auto* value = field(object, name);
    if (!value) return Result<std::array<float, N>>::success(fallback);
    const auto* array = value->getIf<Value::Array>();
    if (!array || array->size() != N)
        return failure<std::array<float, N>>(DiagnosticCode::ParseError, "terrain vector is invalid",
                                             std::string(name));
    for (std::size_t i = 0; i < N; ++i) {
        Value::Object scalar{{"value", (*array)[i]}};
        auto          parsed = numberField(scalar, "value", 0);
        if (!parsed) return Result<std::array<float, N>>::failure(parsed.status());
        fallback[i] = parsed.value();
    }
    return Result<std::array<float, N>>::success(fallback);
}

}  // namespace

Result<LoadedTerrainMaterial> EvpackTerrainMaterialLoader::load(const AssetRef&                  material,
                                                                const asset::EvpackCapabilities& capabilities,
                                                                const TerrainMaterialLoadLimits& limits) const {
    auto payload = reader_.read(material, "eve.terrain-material/3", capabilities, limits.maximumDecodedBytes);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader_.read(material, "eve.terrain-material/2", capabilities, limits.maximumDecodedBytes);
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
        return failure<LoadedTerrainMaterial>(DiagnosticCode::NotFound, "terrain material definition is missing");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes       = limits.maximumDecodedBytes;
    definitionLimits.maximumStringBytes = limits.maximumStringBytes;
    auto metadata                       = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedTerrainMaterial>::failure(metadata.status());
    const auto*  root        = metadata.value().getIf<Value::Object>();
    const Value* schema      = root ? field(*root, "schema") : nullptr;
    const Value* version     = root ? field(*root, "schemaVersion") : nullptr;
    const Value* layersValue = root ? field(*root, "layers") : nullptr;
    const auto*  layers      = layersValue ? layersValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.terrain-material" || !version ||
        !version->isInt64() || (version->asInt() != 2 && version->asInt() != 3) || !layers || layers->empty() ||
        layers->size() > limits.maximumLayers)
        return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError, "terrain material envelope is invalid");
    std::vector<RuntimeTerrainLayer> decoded;
    decoded.reserve(layers->size());
    for (std::size_t index = 0; index < layers->size(); ++index) {
        const auto* layer = (*layers)[index].getIf<Value::Object>();
        if (!layer)
            return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError, "terrain layer must be an object",
                                                  std::to_string(index));
        auto         name       = textField(*layer, "name", limits, false);
        auto         diffuse    = textField(*layer, "diffuseSource", limits, true);
        auto         normal     = textField(*layer, "normalSource", limits, true);
        auto         weight     = textField(*layer, "weightSource", limits, true);
        auto         convention = textField(*layer, "normalConvention", limits, false);
        const Value* tile       = field(*layer, "tileSizeMeters");
        if (!name || !diffuse || !normal || !weight || !convention || !tile || (!tile->isDouble() && !tile->isInt64()))
            return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError, "terrain layer fields are incomplete",
                                                  std::to_string(index));
        const double tileSize = tile->isDouble() ? tile->asDouble() : double(tile->asInt());
        if (!std::isfinite(tileSize) || tileSize <= 0 || tileSize > 1'000'000.0 ||
            (convention.value() != "opengl" && convention.value() != "directx"))
            return failure<LoadedTerrainMaterial>(
                DiagnosticCode::InvalidArgument, "terrain layer physical semantics are invalid", std::to_string(index));
        RuntimeTerrainLayer runtime;
        runtime.name             = std::move(name).takeValue();
        runtime.diffuseSource    = std::move(diffuse).takeValue();
        runtime.normalSource     = std::move(normal).takeValue();
        runtime.weightSource     = std::move(weight).takeValue();
        runtime.normalConvention = std::move(convention).takeValue();
        runtime.tileSizeMeters   = static_cast<float>(tileSize);
        if (version->asInt() >= 2) {
            auto mask        = textField(*layer, "maskSource", limits, true);
            auto tileScale   = vectorField<2>(*layer, "tileScaleMeters", {1, 1});
            auto tileOffset  = vectorField<2>(*layer, "tileOffsetMeters", {});
            auto remapMin    = vectorField<4>(*layer, "maskRemapMinimum", {});
            auto remapMax    = vectorField<4>(*layer, "maskRemapMaximum", {1, 1, 1, 1});
            auto specular    = vectorField<4>(*layer, "specular", {});
            auto metallic    = numberField(*layer, "metallic", 0);
            auto normalScale = numberField(*layer, "normalScale", 1);
            auto smoothness  = numberField(*layer, "smoothness", 0);
            if (!mask || !tileScale || !tileOffset || !remapMin || !remapMax || !specular || !metallic ||
                !normalScale || !smoothness)
                return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                                      "terrain layer v2 fields are incomplete", std::to_string(index));
            if (tileScale.value()[0] <= 0 || tileScale.value()[1] <= 0 || metallic.value() < 0 ||
                metallic.value() > 1 || normalScale.value() < -8 || normalScale.value() > 8 || smoothness.value() < 0 ||
                smoothness.value() > 1)
                return failure<LoadedTerrainMaterial>(DiagnosticCode::InvalidArgument,
                                                      "terrain layer v2 physical semantics are invalid",
                                                      std::to_string(index));
            runtime.maskSource       = std::move(mask).takeValue();
            runtime.tileScaleMeters  = std::move(tileScale).takeValue();
            runtime.tileOffsetMeters = std::move(tileOffset).takeValue();
            runtime.maskRemapMinimum = std::move(remapMin).takeValue();
            runtime.maskRemapMaximum = std::move(remapMax).takeValue();
            runtime.specular         = std::move(specular).takeValue();
            runtime.metallic         = metallic.value();
            runtime.normalScale      = normalScale.value();
            runtime.smoothness       = smoothness.value();
            if (version->asInt() >= 3) {
                auto diffuseAsset = assetField(*layer, "diffuseAsset", limits);
                auto normalAsset  = assetField(*layer, "normalAsset", limits);
                auto weightAsset  = assetField(*layer, "weightAsset", limits);
                auto maskAsset    = assetField(*layer, "maskAsset", limits);
                if (!diffuseAsset || !normalAsset || !weightAsset || !maskAsset)
                    return failure<LoadedTerrainMaterial>(
                        DiagnosticCode::ParseError, "terrain layer v3 AssetRefs are invalid", std::to_string(index));
                runtime.diffuseAsset = std::move(diffuseAsset).takeValue();
                runtime.normalAsset  = std::move(normalAsset).takeValue();
                runtime.weightAsset  = std::move(weightAsset).takeValue();
                runtime.maskAsset    = std::move(maskAsset).takeValue();
            }
        }
        decoded.push_back(std::move(runtime));
    }
    std::string                            holes;
    std::array<std::string, 4>             controls;
    std::optional<AssetRef>                holesAsset;
    std::array<std::optional<AssetRef>, 4> controlAssets;
    float                                  boundsMultiplier = 1;
    if (version->asInt() >= 2) {
        auto        parsedHoles     = textField(*root, "holesSource", limits, true);
        const auto* encodedControls = field(*root, "controlSources");
        const auto* controlArray    = encodedControls ? encodedControls->getIf<Value::Array>() : nullptr;
        auto        parsedBounds    = numberField(*root, "boundsMultiplier", 1);
        if (!parsedHoles || !controlArray || controlArray->size() != controls.size() || !parsedBounds)
            return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                                  "terrain material v2 fields are incomplete");
        for (std::size_t i = 0; i < controls.size(); ++i) {
            if (!(*controlArray)[i].isString() || (*controlArray)[i].asString().size() > limits.maximumStringBytes ||
                !isValidUtf8((*controlArray)[i].asString(), Utf8NullPolicy::Reject))
                return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError, "terrain control source is invalid",
                                                      std::to_string(i));
            controls[i] = (*controlArray)[i].asString();
        }
        if (parsedBounds.value() <= 0 || parsedBounds.value() > 1'000'000)
            return failure<LoadedTerrainMaterial>(DiagnosticCode::InvalidArgument,
                                                  "terrain bounds multiplier is invalid");
        holes            = std::move(parsedHoles).takeValue();
        boundsMultiplier = parsedBounds.value();
        if (version->asInt() >= 3) {
            auto        parsedHolesAsset     = assetField(*root, "holesAsset", limits);
            const auto* encodedControlAssets = field(*root, "controlAssets");
            const auto* controlAssetArray =
                encodedControlAssets ? encodedControlAssets->getIf<Value::Array>() : nullptr;
            if (!parsedHolesAsset || !controlAssetArray || controlAssetArray->size() != controlAssets.size())
                return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                                      "terrain material v3 AssetRefs are incomplete");
            holesAsset = std::move(parsedHolesAsset).takeValue();
            for (std::size_t i = 0; i < controlAssets.size(); ++i) {
                Value::Object encoded{{"asset", (*controlAssetArray)[i]}};
                auto          parsed = assetField(encoded, "asset", limits);
                if (!parsed)
                    return failure<LoadedTerrainMaterial>(DiagnosticCode::ParseError,
                                                          "terrain control AssetRef is invalid", std::to_string(i));
                controlAssets[i] = std::move(parsed).takeValue();
            }
        }
    }
    LoadedTerrainMaterial result{material};
    result.layers           = std::move(decoded);
    result.holesSource      = std::move(holes);
    result.controlSources   = std::move(controls);
    result.holesAsset       = std::move(holesAsset);
    result.controlAssets    = std::move(controlAssets);
    result.boundsMultiplier = boundsMultiplier;
    result.variant          = std::move(payload).takeValue().variant;
    return Result<LoadedTerrainMaterial>::success(std::move(result));
}

}  // namespace eve::asset_procgen
