#include "asset/EvpackResourceReader.h"

#include <algorithm>
#include <tuple>

namespace eve::asset {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.evpack.reader"));
}
}  // namespace

Result<RuntimeAssetPayload> EvpackResourceReader::read(
    const AssetRef& asset, std::string_view expectedType,
    const EvpackCapabilities& capabilities, std::uint64_t maximumDecodedBytes) const {
    if (!pack_ || asset.id().isNil() || expectedType.empty())
        return fail<RuntimeAssetPayload>(DiagnosticCode::InvalidArgument,
                                        "pack, asset and expected type are required");
    auto selected = selectEvpackVariant(*pack_, capabilities);
    if (!selected) return Result<RuntimeAssetPayload>::failure(selected.status());
    RuntimeAssetPayload result{asset, {}, {}, selected.value(), {}};
    bool identityFound = false;
    bool typeFound = false;
    std::uint64_t decodedTotal = 0;
    for (std::size_t index = 0; index < pack_->chunks().size(); ++index) {
        const auto& chunk = pack_->chunks()[index];
        if (chunk.assetId != asset.id()) continue;
        identityFound = true;
        const std::string actualType = chunk.type + "/" + std::to_string(chunk.schemaVersion.value());
        if (actualType != expectedType) continue;
        typeFound = true;
        if (chunk.variantIndex != selected.value().index) continue;
        if (decodedTotal > maximumDecodedBytes || chunk.decodedSize > maximumDecodedBytes - decodedTotal)
            return fail<RuntimeAssetPayload>(DiagnosticCode::InvalidArgument,
                                            "asset payload exceeds decoded budget", asset.format());
        auto bytes = pack_->decodeChunk(index, maximumDecodedBytes - decodedTotal);
        if (!bytes) return Result<RuntimeAssetPayload>::failure(bytes.status());
        decodedTotal += bytes.value().size();
        result.type = chunk.type;
        result.schemaVersion = chunk.schemaVersion;
        result.chunks.push_back({chunk.kind, chunk.chunkId, std::move(bytes).takeValue()});
    }
    if (result.chunks.empty()) {
        if (identityFound && !typeFound)
            return fail<RuntimeAssetPayload>(DiagnosticCode::TypeMismatch,
                                            "asset does not provide the expected type/version",
                                            asset.format());
        if (typeFound)
            return fail<RuntimeAssetPayload>(DiagnosticCode::Unsupported,
                                            "asset has no chunk for the selected variant", asset.format());
        return fail<RuntimeAssetPayload>(DiagnosticCode::NotFound, "asset is not present", asset.format());
    }
    std::sort(result.chunks.begin(), result.chunks.end(), [](const auto& left, const auto& right) {
        return std::tie(left.kind, left.chunkId) < std::tie(right.kind, right.chunkId);
    });
    if (result.chunks.front().kind != EvpackChunkKind::Definition)
        return fail<RuntimeAssetPayload>(DiagnosticCode::InvariantViolation,
                                        "asset has no definition chunk", asset.format());
    return Result<RuntimeAssetPayload>::success(std::move(result));
}
}  // namespace eve::asset
