#include "asset/EvpackRegistry.h"
#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <set>
#include <tuple>
#include "asset/EvpackResourceReader.h"

// EvpackRegistry.cpp
namespace eve::asset {
namespace {

template <class Entry>
bool sameGeneration(const EvpackHandle& handle, const Entry& entry) {
    return handle.generation == entry.generation && handle.buildId == entry.pack->buildId();
}

std::vector<Diagnostic> dispatchCallbacks(const std::vector<EvpackRegistry::Callback>& callbacks,
                                          const EvpackRegistryEvent&                   event) {
    std::vector<Diagnostic> diagnostics;
    for (const auto& callback : callbacks) {
        try {
            auto notified = callback(event);
            if (!notified && notified.error()) diagnostics.push_back(*notified.error());
        } catch (const std::exception& error) {
            diagnostics.push_back(Diagnostic::error(DiagnosticCode::CallbackFailure,
                                                    "registry callback threw: " + std::string(error.what()),
                                                    event.handle.packageId.format(), {}, "asset.evpack.registry"));
        } catch (...) {
            diagnostics.push_back(Diagnostic::error(DiagnosticCode::CallbackFailure,
                                                    "registry callback threw a non-standard exception",
                                                    event.handle.packageId.format(), {}, "asset.evpack.registry"));
        }
    }
    return diagnostics;
}

}  // namespace

Result<PreparedEvpackMount> prepareEvpackMount(std::span<const std::uint8_t> bytes, const EvpackLimits& limits,
                                               const EvpackTrust& trust) {
    auto parsed = parseEvpack(bytes, limits, trust);
    if (!parsed) return Result<PreparedEvpackMount>::failure(parsed.status());
    auto pack = std::make_shared<const Evpack>(std::move(parsed).takeValue());
    return Result<PreparedEvpackMount>::success(PreparedEvpackMount(std::move(pack)));
}

Result<EvpackRegistrySubscription> EvpackRegistry::subscribe(Callback callback) {
    if (!callback)
        return Result<EvpackRegistrySubscription>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "registry callback is empty", {}, {}, "asset.evpack.registry"));
    std::lock_guard lock(mutex_);
    if (nextSubscription_ == std::numeric_limits<std::uint64_t>::max())
        return Result<EvpackRegistrySubscription>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "registry subscription identity is exhausted", {}, {},
                              "asset.evpack.registry"));
    const auto id = ++nextSubscription_;
    callbacks_.emplace(id, std::move(callback));
    return Result<EvpackRegistrySubscription>::success({id});
}

Result<void> EvpackRegistry::unsubscribe(EvpackRegistrySubscription subscription) {
    if (subscription.value == 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "registry subscription identity is invalid", {}, {},
                                                       "asset.evpack.registry"));
    std::lock_guard lock(mutex_);
    if (callbacks_.erase(subscription.value) == 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "registry subscription is not active",
                                                       {}, {}, "asset.evpack.registry"));
    return Result<void>::success();
}

Result<EvpackMountReceipt> EvpackRegistry::commit(PreparedEvpackMount candidate) {
    if (!candidate.pack_)
        return Result<EvpackMountReceipt>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "mount candidate has already been consumed", {},
                                                                     {}, "asset.evpack.registry"));
    const PersistentId    packageId = candidate.pack_->packageId();
    bool                  replaced  = false;
    EvpackHandle          handle;
    std::vector<Callback> callbacks;
    {
        std::lock_guard        lock(mutex_);
        std::set<PersistentId> available;
        std::set<PersistentId> candidateAssets;
        for (const auto& chunk : candidate.pack_->chunks()) {
            available.emplace(chunk.assetId);
            candidateAssets.emplace(chunk.assetId);
        }
        for (const auto& [mountedId, entry] : packages_) {
            if (mountedId == packageId) continue;
            for (const auto& chunk : entry.pack->chunks()) {
                if (candidateAssets.contains(chunk.assetId))
                    return Result<EvpackMountReceipt>::failure(Diagnostic::error(
                        DiagnosticCode::Conflict, "runtime asset identity already has another mounted provider",
                        chunk.assetId.format(), {}, "asset.evpack.registry"));
                available.emplace(chunk.assetId);
            }
        }
        for (const auto& chunk : candidate.pack_->chunks()) {
            for (const auto& dependency : chunk.dependencies) {
                if (!available.contains(dependency))
                    return Result<EvpackMountReceipt>::failure(
                        Diagnostic::error(DiagnosticCode::NotFound, "required runtime dependency is not mounted",
                                          dependency.format(), {}, "asset.evpack.registry"));
            }
        }
        for (const auto& [mountedId, entry] : packages_) {
            if (mountedId == packageId) continue;
            for (const auto& chunk : entry.pack->chunks()) {
                for (const auto& dependency : chunk.dependencies) {
                    if (!available.contains(dependency))
                        return Result<EvpackMountReceipt>::failure(Diagnostic::error(
                            DiagnosticCode::NotFound, "replacement would invalidate a mounted package dependency",
                            dependency.format(), {}, "asset.evpack.registry"));
                }
            }
        }
        replaced            = packages_.contains(packageId);
        std::uint64_t& next = nextGenerations_[packageId];
        if (next == std::numeric_limits<std::uint64_t>::max())
            return Result<EvpackMountReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "package generation is exhausted",
                                  packageId.format(), {}, "asset.evpack.registry"));
        ++next;
        auto pack = std::move(candidate.pack_);
        handle    = {packageId, pack->buildId(), next};
        packages_.insert_or_assign(packageId, Entry{std::move(pack), next});
        callbacks.reserve(callbacks_.size());
        for (const auto& [id, callback] : callbacks_) {
            (void)id;
            callbacks.push_back(callback);
        }
    }
    const EvpackRegistryEvent event{replaced ? EvpackRegistryEventKind::Replaced : EvpackRegistryEventKind::Mounted,
                                    handle};
    auto callbackDiagnostics = dispatchCallbacks(callbacks, event);
    return Result<EvpackMountReceipt>::success({handle, replaced, std::move(callbackDiagnostics)});
}

Result<std::shared_ptr<const Evpack>> EvpackRegistry::resolve(const EvpackHandle& handle) const {
    std::lock_guard lock(mutex_);
    const auto      found = packages_.find(handle.packageId);
    if (found == packages_.end())
        return Result<std::shared_ptr<const Evpack>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "runtime package is not mounted", handle.packageId.format(), {},
                              "asset.evpack.registry"));
    if (!sameGeneration(handle, found->second))
        return Result<std::shared_ptr<const Evpack>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "runtime package handle is stale", handle.packageId.format(),
                              {}, "asset.evpack.registry"));
    return Result<std::shared_ptr<const Evpack>>::success(found->second.pack);
}

Result<EvpackAssetHandle> EvpackRegistry::resolveAsset(const AssetRef& assetRef, std::string_view expectedType,
                                                       const EvpackCapabilities& capabilities) const {
    if (expectedType.empty())
        return Result<EvpackAssetHandle>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "expected runtime asset type is empty", {}, {}, "asset.evpack.registry"));
    std::lock_guard lock(mutex_);
    bool            identityFound = false;
    bool            typeFound     = false;
    for (const auto& [packageId, entry] : packages_) {
        const EvpackChunk* representative = nullptr;
        for (const auto& chunk : entry.pack->chunks()) {
            if (chunk.assetId != assetRef.id()) continue;
            identityFound            = true;
            representative           = &chunk;
            const std::string actual = chunk.type + "/" + std::to_string(chunk.schemaVersion.value());
            if (actual == expectedType)
                typeFound = true;
            else
                return Result<EvpackAssetHandle>::failure(Diagnostic::error(
                    DiagnosticCode::TypeMismatch, "runtime asset provider has a different canonical type",
                    assetRef.format(), {}, "asset.evpack.registry"));
        }
        if (!representative) continue;
        auto selected = selectEvpackVariant(*entry.pack, capabilities);
        if (!selected) continue;
        return Result<EvpackAssetHandle>::success(
            {{packageId, entry.pack->buildId(), entry.generation}, assetRef, std::string(expectedType), capabilities});
    }
    if (identityFound && typeFound)
        return Result<EvpackAssetHandle>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                                    "asset provider has no compatible variant",
                                                                    assetRef.format(), {}, "asset.evpack.registry"));
    return Result<EvpackAssetHandle>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                                "runtime asset has no mounted provider",
                                                                assetRef.format(), {}, "asset.evpack.registry"));
}

Result<RuntimeAssetPayload> EvpackRegistry::readAsset(const EvpackAssetHandle& handle,
                                                      std::uint64_t            maximumDecodedBytes) const {
    auto pack = resolve(handle.package);
    if (!pack) return Result<RuntimeAssetPayload>::failure(pack.status());
    EvpackResourceReader reader(std::move(pack).takeValue());
    return reader.read(handle.asset, handle.expectedType, handle.capabilities, maximumDecodedBytes);
}

Result<EvpackChunkHandle> EvpackRegistry::resolveChunk(const EvpackHandle& handle, const PersistentId& assetId,
                                                       std::string_view type, EvpackChunkKind kind,
                                                       std::uint32_t chunkId, std::uint32_t variantIndex) const {
    auto resolved = resolve(handle);
    if (!resolved) return Result<EvpackChunkHandle>::failure(resolved.status());
    const auto pack = std::move(resolved).takeValue();
    for (std::size_t index = 0; index < pack->chunks().size(); ++index) {
        const auto& chunk = pack->chunks()[index];
        if (chunk.assetId == assetId && chunk.type == type && chunk.kind == kind && chunk.chunkId == chunkId &&
            chunk.variantIndex == variantIndex)
            return Result<EvpackChunkHandle>::success({handle, static_cast<std::uint32_t>(index)});
    }
    return Result<EvpackChunkHandle>::failure(Diagnostic::error(
        DiagnosticCode::NotFound, "runtime chunk is not present", assetId.format(), {}, "asset.evpack.registry"));
}

Result<std::vector<std::uint8_t>> EvpackRegistry::copyChunkBytes(const EvpackChunkHandle& handle) const {
    auto resolved = resolve(handle.package);
    if (!resolved) return Result<std::vector<std::uint8_t>>::failure(resolved.status());
    const auto pack = std::move(resolved).takeValue();
    if (handle.chunkIndex >= pack->chunks().size())
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "chunk index is outside the TOC", {}, {}, "asset.evpack.registry"));
    return pack->decodeChunk(handle.chunkIndex, pack->chunks()[handle.chunkIndex].decodedSize);
}

Result<EvpackUnmountReceipt> EvpackRegistry::unmount(const EvpackHandle& handle) {
    std::vector<Callback> callbacks;
    {
        std::lock_guard lock(mutex_);
        const auto      found = packages_.find(handle.packageId);
        if (found == packages_.end())
            return Result<EvpackUnmountReceipt>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "runtime package is not mounted", {}, {}, "asset.evpack.registry"));
        if (!sameGeneration(handle, found->second))
            return Result<EvpackUnmountReceipt>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "runtime package handle is stale", {}, {}, "asset.evpack.registry"));
        packages_.erase(found);
        callbacks.reserve(callbacks_.size());
        for (const auto& [id, callback] : callbacks_) {
            (void)id;
            callbacks.push_back(callback);
        }
    }
    const EvpackRegistryEvent event{EvpackRegistryEventKind::Unmounted, handle};
    auto                      callbackDiagnostics = dispatchCallbacks(callbacks, event);
    return Result<EvpackUnmountReceipt>::success({handle, std::move(callbackDiagnostics)});
}

}  // namespace eve::asset

// EvpackResourceReader.cpp
namespace eve::asset {
namespace {}  // namespace

Result<RuntimeAssetPayload> EvpackResourceReader::read(const AssetRef& asset, std::string_view expectedType,
                                                       const EvpackCapabilities& capabilities,
                                                       std::uint64_t             maximumDecodedBytes) const {
    if (!pack_ || asset.id().isNil() || expectedType.empty())
        return Result<RuntimeAssetPayload>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "pack, asset and expected type are required", {},
                                                                      {}, "asset.evpack.reader"));
    auto selected = selectEvpackVariant(*pack_, capabilities);
    if (!selected) return Result<RuntimeAssetPayload>::failure(selected.status());
    RuntimeAssetPayload result{asset, {}, {}, selected.value(), {}};
    bool                identityFound = false;
    bool                typeFound     = false;
    std::uint64_t       decodedTotal  = 0;
    for (std::size_t index = 0; index < pack_->chunks().size(); ++index) {
        const auto& chunk = pack_->chunks()[index];
        if (chunk.assetId != asset.id()) continue;
        identityFound                = true;
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
        result.type          = chunk.type;
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

Result<std::vector<AssetRef>> EvpackResourceReader::listAssets(std::string_view          expectedType,
                                                               const EvpackCapabilities& capabilities,
                                                               std::uint32_t             maximumAssets) const {
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
