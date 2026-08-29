#include "asset/EvpackRegistry.h"

#include <limits>
#include <set>
#include <exception>

namespace eve::asset {
namespace {

template <class T>
Result<T> registryFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.evpack.registry"));
}

Result<void> registryFailureVoid(DiagnosticCode code, std::string message) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), {}, {},
                                                   "asset.evpack.registry"));
}

template <class Entry>
bool sameGeneration(const EvpackHandle& handle, const Entry& entry) {
    return handle.generation == entry.generation && handle.buildId == entry.pack->buildId();
}

std::vector<Diagnostic> dispatchCallbacks(
    const std::vector<EvpackRegistry::Callback>& callbacks,
    const EvpackRegistryEvent& event) {
    std::vector<Diagnostic> diagnostics;
    for (const auto& callback : callbacks) {
        try {
            auto notified = callback(event);
            if (!notified && notified.error()) diagnostics.push_back(*notified.error());
        } catch (const std::exception& error) {
            diagnostics.push_back(Diagnostic::error(DiagnosticCode::CallbackFailure,
                                                     "registry callback threw: " + std::string(error.what()),
                                                     event.handle.packageId.format(), {},
                                                     "asset.evpack.registry"));
        } catch (...) {
            diagnostics.push_back(Diagnostic::error(DiagnosticCode::CallbackFailure,
                                                     "registry callback threw a non-standard exception",
                                                     event.handle.packageId.format(), {},
                                                     "asset.evpack.registry"));
        }
    }
    return diagnostics;
}

}  // namespace

Result<PreparedEvpackMount> prepareEvpackMount(std::span<const std::uint8_t> bytes,
                                               const EvpackLimits& limits,
                                               const EvpackTrust& trust) {
    auto parsed = parseEvpack(bytes, limits, trust);
    if (!parsed) return Result<PreparedEvpackMount>::failure(parsed.status());
    auto pack = std::make_shared<const Evpack>(std::move(parsed).takeValue());
    return Result<PreparedEvpackMount>::success(PreparedEvpackMount(std::move(pack)));
}

Result<EvpackRegistrySubscription> EvpackRegistry::subscribe(Callback callback) {
    if (!callback)
        return registryFailure<EvpackRegistrySubscription>(DiagnosticCode::InvalidArgument,
                                                           "registry callback is empty");
    std::lock_guard lock(mutex_);
    if (nextSubscription_ == std::numeric_limits<std::uint64_t>::max())
        return registryFailure<EvpackRegistrySubscription>(DiagnosticCode::InvariantViolation,
                                                           "registry subscription identity is exhausted");
    const auto id = ++nextSubscription_;
    callbacks_.emplace(id, std::move(callback));
    return Result<EvpackRegistrySubscription>::success({id});
}

Result<void> EvpackRegistry::unsubscribe(EvpackRegistrySubscription subscription) {
    if (subscription.value == 0)
        return registryFailureVoid(DiagnosticCode::InvalidArgument,
                                   "registry subscription identity is invalid");
    std::lock_guard lock(mutex_);
    if (callbacks_.erase(subscription.value) == 0)
        return registryFailureVoid(DiagnosticCode::NotFound,
                                   "registry subscription is not active");
    return Result<void>::success();
}

Result<EvpackMountReceipt> EvpackRegistry::commit(PreparedEvpackMount candidate) {
    if (!candidate.pack_)
        return registryFailure<EvpackMountReceipt>(DiagnosticCode::InvalidArgument,
                                                   "mount candidate has already been consumed");
    const PersistentId packageId = candidate.pack_->packageId();
    bool replaced = false;
    EvpackHandle handle;
    std::vector<Callback> callbacks;
    {
        std::lock_guard lock(mutex_);
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
                    return registryFailure<EvpackMountReceipt>(
                        DiagnosticCode::Conflict,
                        "runtime asset identity already has another mounted provider",
                        chunk.assetId.format());
                available.emplace(chunk.assetId);
            }
        }
        for (const auto& chunk : candidate.pack_->chunks()) {
            for (const auto& dependency : chunk.dependencies) {
                if (!available.contains(dependency))
                    return registryFailure<EvpackMountReceipt>(
                        DiagnosticCode::NotFound, "required runtime dependency is not mounted",
                        dependency.format());
            }
        }
        for (const auto& [mountedId, entry] : packages_) {
            if (mountedId == packageId) continue;
            for (const auto& chunk : entry.pack->chunks()) {
                for (const auto& dependency : chunk.dependencies) {
                    if (!available.contains(dependency))
                        return registryFailure<EvpackMountReceipt>(
                            DiagnosticCode::NotFound,
                            "replacement would invalidate a mounted package dependency",
                            dependency.format());
                }
            }
        }
        replaced = packages_.contains(packageId);
        std::uint64_t& next = nextGenerations_[packageId];
        if (next == std::numeric_limits<std::uint64_t>::max())
            return registryFailure<EvpackMountReceipt>(DiagnosticCode::InvariantViolation,
                                                       "package generation is exhausted", packageId.format());
        ++next;
        auto pack = std::move(candidate.pack_);
        handle = {packageId, pack->buildId(), next};
        packages_.insert_or_assign(packageId, Entry{std::move(pack), next});
        callbacks.reserve(callbacks_.size());
        for (const auto& [id, callback] : callbacks_) {
            (void)id;
            callbacks.push_back(callback);
        }
    }
    const EvpackRegistryEvent event{replaced ? EvpackRegistryEventKind::Replaced
                                             : EvpackRegistryEventKind::Mounted,
                                    handle};
    auto callbackDiagnostics = dispatchCallbacks(callbacks, event);
    return Result<EvpackMountReceipt>::success({handle, replaced, std::move(callbackDiagnostics)});
}

Result<std::shared_ptr<const Evpack>> EvpackRegistry::resolve(const EvpackHandle& handle) const {
    std::lock_guard lock(mutex_);
    const auto found = packages_.find(handle.packageId);
    if (found == packages_.end())
        return registryFailure<std::shared_ptr<const Evpack>>(DiagnosticCode::NotFound,
                                                              "runtime package is not mounted",
                                                              handle.packageId.format());
    if (!sameGeneration(handle, found->second))
        return registryFailure<std::shared_ptr<const Evpack>>(DiagnosticCode::StaleHandle,
                                                              "runtime package handle is stale",
                                                              handle.packageId.format());
    return Result<std::shared_ptr<const Evpack>>::success(found->second.pack);
}

Result<EvpackAssetHandle> EvpackRegistry::resolveAsset(
    const AssetRef& assetRef, std::string_view expectedType,
    const EvpackCapabilities& capabilities) const {
    if (expectedType.empty())
        return registryFailure<EvpackAssetHandle>(DiagnosticCode::InvalidArgument,
                                                  "expected runtime asset type is empty");
    std::lock_guard lock(mutex_);
    bool identityFound = false;
    bool typeFound = false;
    for (const auto& [packageId, entry] : packages_) {
        const EvpackChunk* representative = nullptr;
        for (const auto& chunk : entry.pack->chunks()) {
            if (chunk.assetId != assetRef.id()) continue;
            identityFound = true;
            representative = &chunk;
            const std::string actual = chunk.type + "/" +
                                       std::to_string(chunk.schemaVersion.value());
            if (actual == expectedType) typeFound = true;
            else
                return registryFailure<EvpackAssetHandle>(
                    DiagnosticCode::TypeMismatch,
                    "runtime asset provider has a different canonical type", assetRef.format());
        }
        if (!representative) continue;
        auto selected = selectEvpackVariant(*entry.pack, capabilities);
        if (!selected) continue;
        return Result<EvpackAssetHandle>::success(
            {{packageId, entry.pack->buildId(), entry.generation}, assetRef,
             std::string(expectedType), capabilities});
    }
    if (identityFound && typeFound)
        return registryFailure<EvpackAssetHandle>(DiagnosticCode::Unsupported,
                                                  "asset provider has no compatible variant",
                                                  assetRef.format());
    return registryFailure<EvpackAssetHandle>(DiagnosticCode::NotFound,
                                              "runtime asset has no mounted provider",
                                              assetRef.format());
}

Result<RuntimeAssetPayload> EvpackRegistry::readAsset(
    const EvpackAssetHandle& handle, std::uint64_t maximumDecodedBytes) const {
    auto pack = resolve(handle.package);
    if (!pack) return Result<RuntimeAssetPayload>::failure(pack.status());
    EvpackResourceReader reader(std::move(pack).takeValue());
    return reader.read(handle.asset, handle.expectedType, handle.capabilities,
                       maximumDecodedBytes);
}

Result<EvpackChunkHandle> EvpackRegistry::resolveChunk(const EvpackHandle& handle,
                                                       const PersistentId& assetId, std::string_view type,
                                                       EvpackChunkKind kind,
                                                       std::uint32_t chunkId,
                                                       std::uint32_t variantIndex) const {
    auto resolved = resolve(handle);
    if (!resolved) return Result<EvpackChunkHandle>::failure(resolved.status());
    const auto pack = std::move(resolved).takeValue();
    for (std::size_t index = 0; index < pack->chunks().size(); ++index) {
        const auto& chunk = pack->chunks()[index];
        if (chunk.assetId == assetId && chunk.type == type && chunk.kind == kind && chunk.chunkId == chunkId &&
            chunk.variantIndex == variantIndex)
            return Result<EvpackChunkHandle>::success({handle, static_cast<std::uint32_t>(index)});
    }
    return registryFailure<EvpackChunkHandle>(DiagnosticCode::NotFound, "runtime chunk is not present",
                                               assetId.format());
}

Result<std::vector<std::uint8_t>> EvpackRegistry::copyChunkBytes(const EvpackChunkHandle& handle) const {
    auto resolved = resolve(handle.package);
    if (!resolved) return Result<std::vector<std::uint8_t>>::failure(resolved.status());
    const auto pack = std::move(resolved).takeValue();
    if (handle.chunkIndex >= pack->chunks().size())
        return registryFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "chunk index is outside the TOC");
    return pack->decodeChunk(handle.chunkIndex, pack->chunks()[handle.chunkIndex].decodedSize);
}

Result<EvpackUnmountReceipt> EvpackRegistry::unmount(const EvpackHandle& handle) {
    std::vector<Callback> callbacks;
    {
        std::lock_guard lock(mutex_);
        const auto found = packages_.find(handle.packageId);
        if (found == packages_.end())
            return registryFailure<EvpackUnmountReceipt>(DiagnosticCode::NotFound,
                                                         "runtime package is not mounted");
        if (!sameGeneration(handle, found->second))
            return registryFailure<EvpackUnmountReceipt>(DiagnosticCode::StaleHandle,
                                                         "runtime package handle is stale");
        packages_.erase(found);
        callbacks.reserve(callbacks_.size());
        for (const auto& [id, callback] : callbacks_) {
            (void)id;
            callbacks.push_back(callback);
        }
    }
    const EvpackRegistryEvent event{EvpackRegistryEventKind::Unmounted, handle};
    auto callbackDiagnostics = dispatchCallbacks(callbacks, event);
    return Result<EvpackUnmountReceipt>::success({handle, std::move(callbackDiagnostics)});
}

}  // namespace eve::asset
