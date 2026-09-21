#include "asset/procgen/EvpackInstanceSetLoader.h"

#include "asset/RuntimeDefinition.h"
#include "common/Utf8Validation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.procgen.instances"));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

float littleFloat(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(little32(bytes, offset));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

}  // namespace

Result<LoadedInstanceSet> EvpackInstanceSetLoader::load(const AssetRef&                  instanceRef,
                                                        const asset::EvpackCapabilities& capabilities,
                                                        const InstanceSetLoadLimits&     limits) const {
    auto payload = reader_.read(instanceRef, "eve.instance-set/5", capabilities, limits.maximumDecodedBytes);
    if (!payload && (payload.error()->code() == DiagnosticCode::NotFound ||
                     payload.error()->code() == DiagnosticCode::TypeMismatch))
        payload = reader_.read(instanceRef, "eve.instance-set/4", capabilities, limits.maximumDecodedBytes);
    if (!payload && (payload.error()->code() == DiagnosticCode::NotFound ||
                     payload.error()->code() == DiagnosticCode::TypeMismatch))
        payload = reader_.read(instanceRef, "eve.instance-set/3", capabilities, limits.maximumDecodedBytes);
    if (!payload && (payload.error()->code() == DiagnosticCode::NotFound ||
                     payload.error()->code() == DiagnosticCode::TypeMismatch))
        payload = reader_.read(instanceRef, "eve.instance-set/2", capabilities, limits.maximumDecodedBytes);
    if (!payload && (payload.error()->code() == DiagnosticCode::NotFound ||
                     payload.error()->code() == DiagnosticCode::TypeMismatch))
        payload = reader_.read(instanceRef, "eve.instance-set/1", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedInstanceSet>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    const asset::RuntimeAssetChunk* bulk       = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind == asset::EvpackChunkKind::Definition) {
            if (definition)
                return failure<LoadedInstanceSet>(DiagnosticCode::Conflict, "instance set has duplicate definitions");
            definition = &chunk;
        } else if (chunk.kind == asset::EvpackChunkKind::Bulk) {
            if (bulk)
                return failure<LoadedInstanceSet>(DiagnosticCode::Conflict, "instance set has duplicate bulk chunks");
            bulk = &chunk;
        }
    }
    static constexpr std::uint8_t magic[] = {'E', 'V', 'I', 'N', 'S', 'T', 0, 1};
    if (!definition || !bulk || bulk->bytes.size() < 16 ||
        !std::equal(std::begin(magic), std::end(magic), bulk->bytes.begin()))
        return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                          "instance definition or EVINST payload is invalid");
    const std::uint32_t   count              = little32(bulk->bytes, 8);
    const std::uint32_t   reserved           = little32(bulk->bytes, 12);
    constexpr std::size_t minimumRecordBytes = 4 + 1 + (3 + 4 + 3) * sizeof(float);
    if (count > limits.maximumInstances || reserved != 0 || count > (bulk->bytes.size() - 16) / minimumRecordBytes)
        return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                          "instance count or reserved header is invalid");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes = limits.maximumDecodedBytes;
    auto metadata                 = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedInstanceSet>::failure(metadata.status());
    const auto*  root          = metadata.value().getIf<Value::Object>();
    const Value* schema        = root ? field(*root, "schema") : nullptr;
    const Value* version       = root ? field(*root, "schemaVersion") : nullptr;
    const Value* declaredCount = root ? field(*root, "count") : nullptr;
    const Value* partition     = root ? field(*root, "partition") : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.instance-set" || !version || !version->isInt64() ||
        (version->asInt() != 1 && version->asInt() != 2 && version->asInt() != 3 && version->asInt() != 4 &&
         version->asInt() != 5) ||
        !declaredCount ||
        !declaredCount->isInt64() ||
        declaredCount->asInt() != count || !partition || !partition->isString() ||
        partition->asString() != "single-cell")
        return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                          "instance metadata does not match EVINST payload");
    LoadedInstanceSet result{instanceRef};
    if (version->asInt() == 5) {
        const Value* windValue = field(*root, "wavingGrass");
        const auto* wind = windValue ? windValue->getIf<Value::Object>() : nullptr;
        auto number = [&](std::string_view name) -> std::optional<float> {
            const Value* value = wind ? field(*wind, name) : nullptr;
            if (!value || !value->isNumeric()) return std::nullopt;
            const double parsed = value->isInt64() ? double(value->asInt()) : value->asDouble();
            if (!std::isfinite(parsed) || parsed < 0 || parsed > std::numeric_limits<float>::max()) return std::nullopt;
            return static_cast<float>(parsed);
        };
        const auto amount = number("amount"), speed = number("speed"), strength = number("strength");
        const Value* tintValue = wind ? field(*wind, "tint") : nullptr;
        const auto* tint = tintValue ? tintValue->getIf<Value::Array>() : nullptr;
        if (!amount || !speed || !strength || !tint || tint->size() != 4)
            return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                              "instance waving grass settings are invalid");
        for (std::size_t component = 0; component < 4; ++component) {
            if (!(*tint)[component].isNumeric())
                return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                                  "instance waving grass tint is invalid");
            const double parsed = (*tint)[component].isInt64() ? double((*tint)[component].asInt())
                                                               : (*tint)[component].asDouble();
            if (!std::isfinite(parsed) || parsed < 0 || parsed > 1)
                return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                                  "instance waving grass tint is invalid");
            result.wavingGrassTint[component] = static_cast<float>(parsed);
        }
        result.wavingGrassAmount = *amount;
        result.wavingGrassSpeed = *speed;
        result.wavingGrassStrength = *strength;
    }
    std::vector<RuntimeInstancePrototype> prototypes;
    std::set<std::string>                 prototypeIds;
    if (version->asInt() >= 2) {
        const Value* prototypeValue = field(*root, "prototypes");
        const auto*  prototypeArray = prototypeValue ? prototypeValue->getIf<Value::Array>() : nullptr;
        if (!prototypeArray || prototypeArray->size() > limits.maximumInstances)
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError, "instance prototype table is invalid");
        prototypes.reserve(prototypeArray->size());
        auto number = [&](const Value::Object& object, std::string_view name) -> std::optional<float> {
            const Value* value = field(object, name);
            if (!value || !value->isNumeric()) return std::nullopt;
            const double parsed = value->isInt64() ? double(value->asInt()) : value->asDouble();
            if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
                parsed > std::numeric_limits<float>::max())
                return std::nullopt;
            return static_cast<float>(parsed);
        };
        for (std::size_t index = 0; index < prototypeArray->size(); ++index) {
            const auto*  object     = (*prototypeArray)[index].getIf<Value::Object>();
            const Value* id         = object ? field(*object, "prototype") : nullptr;
            const Value* mode       = object ? field(*object, "renderMode") : nullptr;
            const Value* mesh       = object ? field(*object, "usePrototypeMesh") : nullptr;
            const Value* instancing = object ? field(*object, "useInstancing") : nullptr;
            const Value* seed       = object ? field(*object, "noiseSeed") : nullptr;
            const Value* resource   = object ? field(*object, "resourceAsset") : nullptr;
            if (!object || !id || !id->isString() || id->asString().empty() ||
                id->asString().size() > limits.maximumStringBytes ||
                !isValidUtf8(id->asString(), Utf8NullPolicy::Reject) || !prototypeIds.emplace(id->asString()).second ||
                !mode || !mode->isString() ||
                (mode->asString() != "GrassBillboard" && mode->asString() != "Grass" &&
                 mode->asString() != "VertexLit") ||
                !mesh || !mesh->isBool() || !instancing || !instancing->isBool() || !seed || !seed->isInt64())
                return failure<LoadedInstanceSet>(DiagnosticCode::ParseError, "instance prototype metadata is invalid",
                                                  std::to_string(index));
            std::string resourceAsset;
            if (version->asInt() >= 3) {
                if (!resource || !resource->isString() || resource->asString().size() > limits.maximumStringBytes ||
                    (!resource->asString().empty() && !AssetRef::parse(resource->asString())))
                    return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                                      "instance prototype resource asset is invalid",
                                                      std::to_string(index));
                resourceAsset = resource->asString();
            }
            const auto minWidth    = number(*object, "minWidth");
            const auto maxWidth    = number(*object, "maxWidth");
            const auto minHeight   = number(*object, "minHeight");
            const auto maxHeight   = number(*object, "maxHeight");
            const auto noiseSpread = number(*object, "noiseSpread");
            const auto density     = number(*object, "density");
            const auto align       = number(*object, "alignToGround");
            const auto jitter      = number(*object, "positionJitter");
            if (!minWidth || !maxWidth || !minHeight || !maxHeight || !noiseSpread || !density || !align || !jitter ||
                *minWidth <= 0 || *maxWidth < *minWidth || *minHeight <= 0 || *maxHeight < *minHeight ||
                *noiseSpread < 0 || *density < 0 || *align < 0 || *align > 1 || *jitter < 0 || *jitter > 1 ||
                (mesh->asBool() != id->asString().starts_with("unity-guid:")))
                return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                                  "instance prototype values are invalid", id->asString());
            RuntimeInstancePrototype prototype{id->asString(), mode->asString(), mesh->asBool(), instancing->asBool(),
                                               *minWidth, *maxWidth, *minHeight, *maxHeight, seed->asInt(),
                                               *noiseSpread, *density, *align, *jitter, std::move(resourceAsset)};
            if (version->asInt() >= 4) {
                auto color = [&](std::string_view name, std::array<float, 4>& output) {
                    const Value* value = field(*object, name);
                    const auto* array = value ? value->getIf<Value::Array>() : nullptr;
                    if (!array || array->size() != 4) return false;
                    for (std::size_t component = 0; component < 4; ++component) {
                        if (!(*array)[component].isNumeric()) return false;
                        const double parsed = (*array)[component].isInt64() ? double((*array)[component].asInt())
                                                                           : (*array)[component].asDouble();
                        if (!std::isfinite(parsed) || parsed < 0 || parsed > 1) return false;
                        output[component] = static_cast<float>(parsed);
                    }
                    return true;
                };
                const auto bend = number(*object, "bendFactor");
                const auto padding = number(*object, "holeEdgePadding");
                const Value* densityScaling = field(*object, "useDensityScaling");
                if (!color("healthyColor", prototype.healthyColor) || !color("dryColor", prototype.dryColor) ||
                    !bend || *bend < 0 || *bend > 1 || !padding || *padding < 0 || *padding > 1 ||
                    !densityScaling || !densityScaling->isBool())
                    return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                                      "instance prototype extended values are invalid", id->asString());
                prototype.bendFactor = *bend;
                prototype.holeEdgePadding = *padding;
                prototype.useDensityScaling = densityScaling->asBool();
            }
            prototypes.push_back(std::move(prototype));
        }
    }
    std::vector<RuntimeInstance> instances;
    instances.reserve(count);
    std::size_t cursor = 16;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (bulk->bytes.size() - cursor < 4)
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError, "instance prototype length is truncated");
        const std::uint32_t stringBytes = little32(bulk->bytes, cursor);
        cursor += 4;
        constexpr std::size_t trsBytes = (3 + 4 + 3) * sizeof(float);
        if (stringBytes == 0 || stringBytes > limits.maximumStringBytes || stringBytes > bulk->bytes.size() - cursor ||
            bulk->bytes.size() - cursor - stringBytes < trsBytes)
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError, "instance record exceeds payload bounds");
        RuntimeInstance instance;
        instance.prototype.assign(reinterpret_cast<const char*>(bulk->bytes.data() + cursor), stringBytes);
        if (!isValidUtf8(instance.prototype, Utf8NullPolicy::Reject))
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError, "instance prototype is not valid UTF-8",
                                              std::to_string(index));
        // Unity tree instances and mesh-backed terrain Details both use unity-guid:. A GUID absent from the
        // Detail prototype table is therefore a tree prototype; texture-backed Details have an unambiguous prefix.
        if (version->asInt() >= 2 && instance.prototype.starts_with("unity-texture-guid:") &&
            !prototypeIds.contains(instance.prototype))
            return failure<LoadedInstanceSet>(DiagnosticCode::NotFound,
                                              "instance references an undeclared terrain detail prototype",
                                              instance.prototype);
        cursor += stringBytes;
        for (float& value : instance.position) {
            value = littleFloat(bulk->bytes, cursor);
            cursor += 4;
        }
        for (float& value : instance.rotation) {
            value = littleFloat(bulk->bytes, cursor);
            cursor += 4;
        }
        for (float& value : instance.scale) {
            value = littleFloat(bulk->bytes, cursor);
            cursor += 4;
        }
        const bool finite =
            std::all_of(instance.position.begin(), instance.position.end(),
                        [](float value) { return std::isfinite(value); }) &&
            std::all_of(instance.rotation.begin(), instance.rotation.end(),
                        [](float value) { return std::isfinite(value); }) &&
            std::all_of(instance.scale.begin(), instance.scale.end(), [](float value) { return std::isfinite(value); });
        const float qLength =
            std::sqrt(instance.rotation[0] * instance.rotation[0] + instance.rotation[1] * instance.rotation[1] +
                      instance.rotation[2] * instance.rotation[2] + instance.rotation[3] * instance.rotation[3]);
        if (!finite || qLength < 0.999f || qLength > 1.001f || instance.scale[0] == 0.f || instance.scale[1] == 0.f ||
            instance.scale[2] == 0.f)
            return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument, "instance TRS is invalid",
                                              std::to_string(index));
        instances.push_back(std::move(instance));
    }
    if (cursor != bulk->bytes.size())
        return failure<LoadedInstanceSet>(DiagnosticCode::ParseError, "instance payload has trailing bytes");
    result.prototypes = std::move(prototypes);
    result.instances = std::move(instances);
    result.variant = std::move(payload).takeValue().variant;
    return Result<LoadedInstanceSet>::success(std::move(result));
}

}  // namespace eve::asset_procgen
