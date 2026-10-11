#include "asset/EvpackResourceReader.h"

#include <algorithm>
#include <new>
#include <tuple>

namespace eve::asset {
namespace {}  // namespace

Result<RuntimeAssetPayload> EvpackResourceReader::read(
    const AssetRef& asset, std::string_view expectedType,
    const EvpackCapabilities& capabilities, std::uint64_t maximumDecodedBytes) const {
    if (!pack_ || asset.id().isNil() || expectedType.empty())
        return Result<RuntimeAssetPayload>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "pack, asset and expected type are required", {},
                                                                      {}, "asset.evpack.reader"));
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
            return Result<RuntimeAssetPayload>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                          "asset payload exceeds decoded budget",
                                                                          asset.format(), {}, "asset.evpack.reader"));
        auto bytes = pack_->decodeChunk(index, maximumDecodedBytes - decodedTotal);
        if (!bytes) return Result<RuntimeAssetPayload>::failure(bytes.status());
        decodedTotal += bytes.value().size();
        result.type = chunk.type;
        result.schemaVersion = chunk.schemaVersion;
        result.chunks.push_back({chunk.kind, chunk.chunkId, std::move(bytes).takeValue()});
    }
    if (result.chunks.empty()) {
        if (identityFound && !typeFound)
            return Result<RuntimeAssetPayload>::failure(
                Diagnostic::error(DiagnosticCode::TypeMismatch, "asset does not provide the expected type/version",
                                  asset.format(), {}, "asset.evpack.reader"));
        if (typeFound)
            return Result<RuntimeAssetPayload>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                                          "asset has no chunk for the selected variant",
                                                                          asset.format(), {}, "asset.evpack.reader"));
        return Result<RuntimeAssetPayload>::failure(Diagnostic::error(DiagnosticCode::NotFound, "asset is not present",
                                                                      asset.format(), {}, "asset.evpack.reader"));
    }
    std::sort(result.chunks.begin(), result.chunks.end(), [](const auto& left, const auto& right) {
        return std::tie(left.kind, left.chunkId) < std::tie(right.kind, right.chunkId);
    });
    if (result.chunks.front().kind != EvpackChunkKind::Definition)
        return Result<RuntimeAssetPayload>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                                      "asset has no definition chunk", asset.format(),
                                                                      {}, "asset.evpack.reader"));
    return Result<RuntimeAssetPayload>::success(std::move(result));
}

Result<std::vector<AssetRef>> EvpackResourceReader::listAssets(
    std::string_view expectedType, const EvpackCapabilities& capabilities, std::uint32_t maximumAssets) const {
    if (!pack_ || expectedType.empty() || maximumAssets == 0)
        return Result<std::vector<AssetRef>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "pack, expected type and asset budget are required", {},
                              {}, "asset.evpack.reader"));
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
            return Result<std::vector<AssetRef>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "asset listing exceeds result budget", {}, {}, "asset.evpack.reader"));
        std::vector<AssetRef> result;
        result.reserve(identities.size());
        for (const auto& identity : identities) {
            auto reference = AssetRef::fromId(identity);
            if (!reference) return Result<std::vector<AssetRef>>::failure(reference.status());
            result.push_back(std::move(reference).takeValue());
        }
        return Result<std::vector<AssetRef>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::vector<AssetRef>>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "asset listing allocation failed", {}, {}, "asset.evpack.reader"));
    }
}
}  // namespace eve::asset
