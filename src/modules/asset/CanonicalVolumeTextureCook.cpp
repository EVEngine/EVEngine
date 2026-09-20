#include "asset/CanonicalVolumeTextureCook.h"

#include "common/Value.h"

#include <limits>

namespace eve::asset {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "asset.cook.volume-texture"));
}
const Value* field(const Value::Object& object, const char* name) {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}
bool exactString(const Value::Object& object, const char* name, const char* expected) {
    const auto* value = field(object, name);
    return value && value->isString() && value->asString() == expected;
}
bool positive32(const Value::Object& object, const char* name, uint32_t& output) {
    const auto* value = field(object, name);
    if (!value || !value->isInt64() || value->asInt() <= 0 || uint64_t(value->asInt()) > UINT32_MAX) return false;
    output = uint32_t(value->asInt());
    return true;
}
void put32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
}
}  // namespace

Result<CookedCanonicalVolumeTexture> cookCanonicalVolumeTextureRgba8(std::span<const uint8_t> definition,
                                                                     std::span<const uint8_t> sourceR8,
                                                                     uint64_t                 maximumDecodedBytes) {
    auto decoded =
        Value::fromJson(std::string_view(reinterpret_cast<const char*>(definition.data()), definition.size()));
    if (!decoded) return Result<CookedCanonicalVolumeTexture>::failure(decoded.status());
    const auto* object = decoded.value().getIf<Value::Object>();
    uint32_t    width = 0, height = 0, depth = 0;
    const auto* blob = object ? field(*object, "blob") : nullptr;
    if (!object || object->size() != 8 || !exactString(*object, "schema", "eve.volume-texture") ||
        !exactString(*object, "encoding", "r8") || !exactString(*object, "usage", "noise") || !blob ||
        !blob->isString() || !blob->asString().ends_with("/source.r8") || !positive32(*object, "width", width) ||
        !positive32(*object, "height", height) || !positive32(*object, "depth", depth))
        return failure<CookedCanonicalVolumeTexture>(DiagnosticCode::ParseError,
                                                     "volume definition shape or members are invalid");
    const auto* version = field(*object, "schemaVersion");
    if (!version || !version->isInt64() || version->asInt() != 1 ||
        uint64_t(width) > std::numeric_limits<uint64_t>::max() / height ||
        uint64_t(width) * height > std::numeric_limits<uint64_t>::max() / depth)
        return failure<CookedCanonicalVolumeTexture>(DiagnosticCode::InvalidArgument,
                                                     "volume dimensions, version, source bytes or budget are invalid");
    const uint64_t voxels = uint64_t(width) * height * depth;
    if (voxels > SIZE_MAX || voxels != sourceR8.size() || maximumDecodedBytes < 24 ||
        voxels > (maximumDecodedBytes - 24) / 4)
        return failure<CookedCanonicalVolumeTexture>(DiagnosticCode::InvalidArgument,
                                                     "volume dimensions, version, source bytes or budget are invalid");

    auto runtime        = *object;
    runtime["encoding"] = Value("rgba8");
    runtime["blob"]     = Value("chunk:1");
    auto encoded        = Value(std::move(runtime)).toJson();
    if (!encoded) return Result<CookedCanonicalVolumeTexture>::failure(encoded.status());
    CookedCanonicalVolumeTexture result;
    result.definition.assign(encoded.value().begin(), encoded.value().end());
    result.bulk.insert(result.bulk.end(), {'E', 'V', 'V', 'O', 'L', 0, 1, 0});
    put32(result.bulk, width);
    put32(result.bulk, height);
    put32(result.bulk, depth);
    put32(result.bulk, 0);
    result.bulk.reserve(24 + size_t(voxels) * 4);
    for (uint8_t value : sourceR8) result.bulk.insert(result.bulk.end(), {value, value, value, 255});
    return Result<CookedCanonicalVolumeTexture>::success(std::move(result));
}

}  // namespace eve::asset
