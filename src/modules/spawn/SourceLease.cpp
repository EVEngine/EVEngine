#include <atomic>
#include <limits>
#include "spawn/SpawnIdentityInternal.h"
#include "spawn/SpawnWorld.h"

namespace eve::spawn {
Result<PersistentId> SourceLease::derive(OperationOrdinal ordinal, std::optional<std::uint32_t> unit) const {
    if (!isCurrent() || !identity_)
        return Result<PersistentId>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Spawn source lease is revoked", "spawn.source"));
    if (ordinal.isZero())
        return Result<PersistentId>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Operation ordinal must be nonzero", "spawn.operation"));
    return Result<PersistentId>::success(
        detail::spawnIdentity(identity_->source.owner, identity_->source.epoch, identity_->id, ordinal.value(), unit));
}
Result<PersistentId> SourceLease::operationIdentity(OperationOrdinal ordinal) const {
    return derive(ordinal, std::nullopt);
}
Result<SubjectRef> SourceLease::unitIdentity(OperationOrdinal ordinal, std::uint32_t index) const {
    auto id = derive(ordinal, index);
    if (!id) return Result<SubjectRef>::failure(id.status());
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(std::move(id).takeValue()));
}
}  // namespace eve::spawn

namespace eve::spawn::detail {
std::uint64_t nextSpawnIncarnation() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto                              value = next.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<std::uint64_t>::max()) {
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
    }
    return 0;
}

PersistentId spawnIdentity(PersistentId owner, std::uint64_t epoch, const std::string& id, std::uint64_t ordinal,
                           std::optional<std::uint32_t> unit) {
    // Flatten the complete role: child() preserves its UUID namespace through XOR.
    auto role = "spawn/epoch/" + std::to_string(epoch) + "/instance/" + id + "/operation/" + std::to_string(ordinal);
    if (unit) role += "/unit/" + std::to_string(*unit);
    return owner.child(role);
}
}  // namespace eve::spawn::detail
