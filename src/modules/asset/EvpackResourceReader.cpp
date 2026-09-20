#include "asset/EvpackResourceReader.h"

#include <algorithm>
#include <new>
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

Result<std::vector<AssetRef>> EvpackResourceReader::listAssets(
    std::string_view expectedType, const EvpackCapabilities& capabilities, std::uint32_t maximumAssets) const {
    if (!pack_ || expectedType.empty() || maximumAssets == 0)
        return fail<std::vector<AssetRef>>(DiagnosticCode::InvalidArgument,
                                          "pack, expected type and asset budget are required");
    auto selected = selectEvpackVariant(*pack_, capabilities);
    if (!selected) return Result<std::vector<AssetRef>>::failure(selected.status());
    try {
        std::vector<PersistentId> identities;
        for (const auto& chunk : pack_->chunks()) {
            const std::string actualType = chunk.type + "/" + std::to_string(chunk.schemaVersion.value());
            if (chunk.variantIndex != selected.value().index || actualType != expectedType) continue;
            identities.push_back(chunk.assetId);
        }
        std::sort(identities.begin(), identities.end());
        identities.erase(std::unique(identities.begin(), identities.end()), identities.end());
        if (identities.size() > maximumAssets)
            return fail<std::vector<AssetRef>>(DiagnosticCode::InvalidArgument,
                                               "asset listing exceeds result budget");
        std::vector<AssetRef> result;
        result.reserve(identities.size());
        for (const auto& identity : identities) {
            auto reference = AssetRef::fromId(identity);
            if (!reference) return Result<std::vector<AssetRef>>::failure(reference.status());
            result.push_back(std::move(reference).takeValue());
        }
        return Result<std::vector<AssetRef>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return fail<std::vector<AssetRef>>(DiagnosticCode::Failed, "asset listing allocation failed");
    }
}
}  // namespace eve::asset
