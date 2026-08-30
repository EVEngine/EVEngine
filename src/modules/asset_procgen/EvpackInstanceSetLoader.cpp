#include "asset_procgen/EvpackInstanceSetLoader.h"

#include "asset/RuntimeDefinition.h"
#include "common/Utf8Validation.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.procgen.instances"));
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

Result<LoadedInstanceSet> EvpackInstanceSetLoader::load(
    const AssetRef& instanceRef, const asset::EvpackCapabilities& capabilities,
    const InstanceSetLoadLimits& limits) const {
    auto payload = reader_.read(instanceRef, "eve.instance-set/1", capabilities,
                                limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedInstanceSet>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind == asset::EvpackChunkKind::Definition) {
            if (definition)
                return failure<LoadedInstanceSet>(DiagnosticCode::Conflict,
                                                  "instance set has duplicate definitions");
            definition = &chunk;
        } else if (chunk.kind == asset::EvpackChunkKind::Bulk) {
            if (bulk)
                return failure<LoadedInstanceSet>(DiagnosticCode::Conflict,
                                                  "instance set has duplicate bulk chunks");
            bulk = &chunk;
        }
    }
    static constexpr std::uint8_t magic[] = {'E', 'V', 'I', 'N', 'S', 'T', 0, 1};
    if (!definition || !bulk || bulk->bytes.size() < 16 ||
        !std::equal(std::begin(magic), std::end(magic), bulk->bytes.begin()))
        return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                          "instance definition or EVINST payload is invalid");
    const std::uint32_t count = little32(bulk->bytes, 8);
    const std::uint32_t reserved = little32(bulk->bytes, 12);
    constexpr std::size_t minimumRecordBytes = 4 + 1 + (3 + 4 + 3) * sizeof(float);
    if (count > limits.maximumInstances || reserved != 0 ||
        count > (bulk->bytes.size() - 16) / minimumRecordBytes)
        return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                          "instance count or reserved header is invalid");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes = limits.maximumDecodedBytes;
    auto metadata = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedInstanceSet>::failure(metadata.status());
    const auto* root = metadata.value().getIf<Value::Object>();
    const Value* schema = root ? field(*root, "schema") : nullptr;
    const Value* version = root ? field(*root, "schemaVersion") : nullptr;
    const Value* declaredCount = root ? field(*root, "count") : nullptr;
    const Value* partition = root ? field(*root, "partition") : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.instance-set" ||
        !version || !version->isInt64() || version->asInt() != 1 || !declaredCount ||
        !declaredCount->isInt64() || declaredCount->asInt() != count || !partition ||
        !partition->isString() || partition->asString() != "single-cell")
        return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                          "instance metadata does not match EVINST payload");
    std::vector<RuntimeInstance> instances;
    instances.reserve(count);
    std::size_t cursor = 16;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (bulk->bytes.size() - cursor < 4)
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                              "instance prototype length is truncated");
        const std::uint32_t stringBytes = little32(bulk->bytes, cursor);
        cursor += 4;
        constexpr std::size_t trsBytes = (3 + 4 + 3) * sizeof(float);
        if (stringBytes == 0 || stringBytes > limits.maximumStringBytes ||
            stringBytes > bulk->bytes.size() - cursor ||
            bulk->bytes.size() - cursor - stringBytes < trsBytes)
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                              "instance record exceeds payload bounds");
        RuntimeInstance instance;
        instance.prototype.assign(reinterpret_cast<const char*>(bulk->bytes.data() + cursor),
                                  stringBytes);
        if (!isValidUtf8(instance.prototype, Utf8NullPolicy::Reject))
            return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                              "instance prototype is not valid UTF-8",
                                              std::to_string(index));
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
        const bool finite = std::all_of(instance.position.begin(), instance.position.end(),
                                        [](float value) { return std::isfinite(value); }) &&
                            std::all_of(instance.rotation.begin(), instance.rotation.end(),
                                        [](float value) { return std::isfinite(value); }) &&
                            std::all_of(instance.scale.begin(), instance.scale.end(),
                                        [](float value) { return std::isfinite(value); });
        const float qLength = std::sqrt(
            instance.rotation[0] * instance.rotation[0] +
            instance.rotation[1] * instance.rotation[1] +
            instance.rotation[2] * instance.rotation[2] +
            instance.rotation[3] * instance.rotation[3]);
        if (!finite || qLength < 0.999f || qLength > 1.001f || instance.scale[0] == 0.f ||
            instance.scale[1] == 0.f || instance.scale[2] == 0.f)
            return failure<LoadedInstanceSet>(DiagnosticCode::InvalidArgument,
                                              "instance TRS is invalid", std::to_string(index));
        instances.push_back(std::move(instance));
    }
    if (cursor != bulk->bytes.size())
        return failure<LoadedInstanceSet>(DiagnosticCode::ParseError,
                                          "instance payload has trailing bytes");
    return Result<LoadedInstanceSet>::success(
        {instanceRef, std::move(instances), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_procgen
