#include "common/Container.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_set>
#include <utility>

namespace eve::container {
namespace {

[[nodiscard]] Result<void> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

[[nodiscard]] const MembershipEntry* findEntry(const ContainerSnapshot& snapshot, const MembershipId& id) {
    const MembershipEntry* found = nullptr;
    for (const auto& entry : snapshot.entries) {
        if (entry.membership.object != id) continue;
        if (found != nullptr) return nullptr;
        found = &entry;
    }
    return found;
}

[[nodiscard]] bool hasDuplicateMemberships(const ContainerSnapshot& snapshot) {
    std::unordered_set<MembershipId> ids;
    std::unordered_set<std::int32_t> slots;
    for (const auto& entry : snapshot.entries) {
        if (!entry.membership.object.isValid() || !ids.insert(entry.membership.object).second) return true;
        if (!entry.membership.slot.isValid() || !slots.insert(entry.membership.slot.value()).second) return true;
        if (entry.object.id != entry.membership.object || entry.object.quantity == 0 ||
            entry.membership.generation.isZero())
            return true;
    }
    return false;
}

[[nodiscard]] Result<void> validateSnapshot(const ContainerSnapshot& snapshot, const IContainer& container) {
    if (!snapshot.id.isValid() || snapshot.id != container.descriptor().id)
        return failure(DiagnosticCode::InvariantViolation, "container snapshot identity does not match adapter");
    if (hasDuplicateMemberships(snapshot))
        return failure(DiagnosticCode::Conflict, "container snapshot contains duplicate or invalid membership");
    const auto& descriptor = container.descriptor();
    if (!descriptor.capacity.isUnlimited() && snapshot.entries.size() > descriptor.capacity.value())
        return failure(DiagnosticCode::InvariantViolation, "container snapshot exceeds capacity");
    if (!descriptor.capacity.isUnlimited()) {
        for (const auto& entry : snapshot.entries) {
            if (static_cast<std::size_t>(entry.membership.slot.value()) >= descriptor.capacity.value())
                return failure(DiagnosticCode::InvariantViolation,
                               "container snapshot contains a slot outside capacity");
        }
    }
    return Result<void>::success();
}

[[nodiscard]] Result<ContainerSnapshot> removeEntry(const ContainerSnapshot&   original,
                                                    const ContainerDescriptor& descriptor, const MembershipId& object) {
    ContainerSnapshot candidate = original;
    const auto        it        = std::find_if(candidate.entries.begin(), candidate.entries.end(),
                                               [&](const MembershipEntry& entry) { return entry.membership.object == object; });
    if (it == candidate.entries.end())
        return Result<ContainerSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "snapshot membership was not found"));
    candidate.entries.erase(it);
    if (descriptor.ordering != Ordering::ExplicitSlots) {
        if (candidate.entries.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return Result<ContainerSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "container has too many indexed memberships"));
        for (std::size_t index = 0; index < candidate.entries.size(); ++index)
            candidate.entries[index].membership.slot = SlotIndex(static_cast<std::int32_t>(index));
    }
    return Result<ContainerSnapshot>::success(std::move(candidate));
}

[[nodiscard]] Result<SlotIndex> chooseDestinationSlot(const ContainerSnapshot&   snapshot,
                                                      const ContainerDescriptor& descriptor,
                                                      std::optional<SlotIndex>   requested) {
    if (requested) {
        if (!requested->isValid())
            return Result<SlotIndex>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "destination slot is invalid"));
        const auto value = static_cast<std::size_t>(requested->value());
        if (descriptor.ordering == Ordering::ExplicitSlots) {
            if (!descriptor.capacity.isUnlimited() && value >= descriptor.capacity.value())
                return Result<SlotIndex>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "destination slot exceeds container capacity"));
            for (const auto& entry : snapshot.entries)
                if (entry.membership.slot == *requested)
                    return Result<SlotIndex>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "destination slot is occupied"));
            return Result<SlotIndex>::success(*requested);
        }
        if (value > snapshot.entries.size())
            return Result<SlotIndex>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "destination slot is out of range"));
        return Result<SlotIndex>::success(*requested);
    }

    if (descriptor.ordering != Ordering::ExplicitSlots) {
        if (snapshot.entries.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return Result<SlotIndex>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "container has too many indexed memberships"));
        return Result<SlotIndex>::success(SlotIndex(static_cast<std::int32_t>(snapshot.entries.size())));
    }

    const std::size_t limit =
        descriptor.capacity.isUnlimited() ? snapshot.entries.size() + 1u : descriptor.capacity.value();
    std::unordered_set<std::int32_t> occupied;
    for (const auto& entry : snapshot.entries) occupied.insert(entry.membership.slot.value());
    for (std::size_t index = 0; index < limit; ++index) {
        if (index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) break;
        if (!occupied.contains(static_cast<std::int32_t>(index)))
            return Result<SlotIndex>::success(SlotIndex(static_cast<std::int32_t>(index)));
    }
    return Result<SlotIndex>::failure(Diagnostic::error(DiagnosticCode::Conflict, "container has no available slot"));
}

[[nodiscard]] Result<ContainerSnapshot> insertEntry(ContainerSnapshot base, const ContainerDescriptor& descriptor,
                                                    const MembershipEntry&   sourceEntry,
                                                    std::optional<SlotIndex> requested) {
    auto destinationResult = chooseDestinationSlot(base, descriptor, requested);
    if (!destinationResult) return Result<ContainerSnapshot>::failure(destinationResult.status());
    const SlotIndex destination = destinationResult.value();
    MembershipEntry moved       = sourceEntry;
    moved.membership.slot       = destination;
    if (descriptor.ordering == Ordering::ExplicitSlots) {
        base.entries.push_back(std::move(moved));
    } else {
        base.entries.insert(base.entries.begin() + destination.value(), std::move(moved));
        for (std::size_t index = 0; index < base.entries.size(); ++index)
            base.entries[index].membership.slot = SlotIndex(static_cast<std::int32_t>(index));
    }
    return Result<ContainerSnapshot>::success(std::move(base));
}

}  // namespace

Result<TransferReceipt> TransferService::transfer(const TransferRequest& request, TransferEventSink eventSink) {
    if (request.source == nullptr || request.destination == nullptr || !request.object.isValid()) {
        return Result<TransferReceipt>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "transfer requires two containers and a membership object"));
    }

    const bool sameContainer        = request.source == request.destination;
    auto       sourceSnapshotResult = request.source->snapshot();
    if (!sourceSnapshotResult) {
        return Result<TransferReceipt>::failure(sourceSnapshotResult.status());
    }
    ContainerSnapshot sourceSnapshot = std::move(sourceSnapshotResult).takeValue();
    ContainerSnapshot destinationSnapshot;
    if (sameContainer) {
        destinationSnapshot = sourceSnapshot;
    } else {
        auto destinationSnapshotResult = request.destination->snapshot();
        if (!destinationSnapshotResult) {
            return Result<TransferReceipt>::failure(destinationSnapshotResult.status());
        }
        destinationSnapshot = std::move(destinationSnapshotResult).takeValue();
    }
    auto sourceSnapshotValid = validateSnapshot(sourceSnapshot, *request.source);
    if (!sourceSnapshotValid) {
        return Result<TransferReceipt>::failure(sourceSnapshotValid.status());
    }
    auto destinationSnapshotValid = validateSnapshot(destinationSnapshot, *request.destination);
    if (!destinationSnapshotValid) {
        return Result<TransferReceipt>::failure(destinationSnapshotValid.status());
    }

    if (request.expectedSourceRevision && *request.expectedSourceRevision != sourceSnapshot.revision) {
        return Result<TransferReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "source container revision is stale"));
    }
    if (request.expectedDestinationRevision && *request.expectedDestinationRevision != destinationSnapshot.revision) {
        return Result<TransferReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "destination container revision is stale"));
    }

    const MembershipEntry* sourceEntry = findEntry(sourceSnapshot, request.object);
    if (sourceEntry == nullptr) {
        bool duplicate = false;
        for (const auto& entry : sourceSnapshot.entries)
            if (entry.membership.object == request.object) duplicate = true;
        return Result<TransferReceipt>::failure(
            Diagnostic::error(duplicate ? DiagnosticCode::Conflict : DiagnosticCode::NotFound,
                              duplicate ? "source contains duplicate membership" : "source membership was not found"));
    }
    if (request.sourceSlot && *request.sourceSlot != sourceEntry->membership.slot) {
        return Result<TransferReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "source slot no longer matches object"));
    }

    if (!sameContainer && request.source->descriptor().id == request.destination->descriptor().id) {
        return Result<TransferReceipt>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "different container adapters must not expose the same container identity"));
    }
    for (const auto& entry : destinationSnapshot.entries) {
        if (entry.membership.object == request.object && !sameContainer)
            return Result<TransferReceipt>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "destination already contains this membership object"));
        if (request.destinationSlot && entry.membership.slot == *request.destinationSlot &&
            !(sameContainer && entry.membership.object == request.object)) {
            return Result<TransferReceipt>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "destination slot is occupied"));
        }
    }

    auto accepted =
        request.destination->validateInsert(sourceEntry->object, request.destinationSlot,
                                            sameContainer ? std::optional<MembershipId>(request.object) : std::nullopt);
    if (!accepted) {
        return Result<TransferReceipt>::failure(accepted.status());
    }

    if (sameContainer && request.destinationSlot && sourceEntry->membership.slot == *request.destinationSlot) {
        TransferReceipt receipt{sourceSnapshot.id,
                                destinationSnapshot.id,
                                request.object,
                                sourceEntry->membership.slot,
                                sourceEntry->membership.slot,
                                sourceSnapshot.revision,
                                destinationSnapshot.revision};
        return Result<TransferReceipt>::success(std::move(receipt), Status::success(StatusCode::NoOp));
    }

    auto sourceRevision = sourceSnapshot.revision.incremented();
    if (!sourceRevision)
        return Result<TransferReceipt>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "source revision exhausted"));
    auto sourceCandidateResult = removeEntry(sourceSnapshot, request.source->descriptor(), request.object);
    if (!sourceCandidateResult) return Result<TransferReceipt>::failure(sourceCandidateResult.status());
    ContainerSnapshot sourceCandidate = std::move(sourceCandidateResult).takeValue();
    sourceCandidate.revision          = *sourceRevision;

    ContainerSnapshot destinationCandidate;
    SlotIndex         destinationSlot = SlotIndex::invalid();
    if (sameContainer) {
        auto candidateResult = insertEntry(std::move(sourceCandidate), request.source->descriptor(), *sourceEntry,
                                           request.destinationSlot);
        if (!candidateResult) {
            return Result<TransferReceipt>::failure(candidateResult.status());
        }
        destinationCandidate          = std::move(candidateResult).takeValue();
        destinationCandidate.revision = *sourceRevision;
        const auto* moved             = findEntry(destinationCandidate, request.object);
        if (moved == nullptr)
            return Result<TransferReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "same-container candidate lost membership"));
        destinationSlot = moved->membership.slot;
    } else {
        auto destinationRevision = destinationSnapshot.revision.incremented();
        if (!destinationRevision)
            return Result<TransferReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "destination revision exhausted"));
        auto candidateResult =
            insertEntry(destinationSnapshot, request.destination->descriptor(), *sourceEntry, request.destinationSlot);
        if (!candidateResult) return Result<TransferReceipt>::failure(candidateResult.status());
        destinationCandidate          = std::move(candidateResult).takeValue();
        destinationCandidate.revision = *destinationRevision;
        const auto* moved             = findEntry(destinationCandidate, request.object);
        if (moved == nullptr)
            return Result<TransferReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "destination candidate lost membership"));
        destinationSlot = moved->membership.slot;
    }

    // For a same-container move, `sourceCandidate` was consumed while building
    // the final candidate.  The adapter must validate and commit that final
    // layout, not the moved-from removal-only intermediate.
    const auto& sourceCommitCandidate = sameContainer ? destinationCandidate : sourceCandidate;
    auto        sourcePreparedResult  = request.source->prepare(sourceSnapshot, sourceCommitCandidate);
    if (!sourcePreparedResult) return Result<TransferReceipt>::failure(sourcePreparedResult.status());
    auto sourcePrepared = std::move(sourcePreparedResult).takeValue();

    if (sameContainer) {
        sourcePrepared->commit();
    } else {
        auto destinationPreparedResult = request.destination->prepare(destinationSnapshot, destinationCandidate);
        if (!destinationPreparedResult) {
            sourcePrepared->rollback();
            return Result<TransferReceipt>::failure(destinationPreparedResult.status());
        }
        auto destinationPrepared = std::move(destinationPreparedResult).takeValue();
        sourcePrepared->commit();
        destinationPrepared->commit();
    }

    TransferReceipt receipt{sourceSnapshot.id,
                            destinationSnapshot.id,
                            request.object,
                            sourceEntry->membership.slot,
                            destinationSlot,
                            *sourceRevision,
                            sameContainer ? *sourceRevision : destinationCandidate.revision};
    Status          resultStatus = Status::success(StatusCode::Applied);
    if (eventSink) {
        try {
            eventSink(TransferEvent{receipt.source, receipt.destination, receipt.object, receipt.sourceSlot,
                                    receipt.destinationSlot, receipt.sourceRevision, receipt.destinationRevision});
        } catch (const std::exception&) {
            try {
                std::vector<Diagnostic> diagnostics;
                diagnostics.emplace_back(Diagnostic::warning(DiagnosticCode::CallbackFailure,
                                                             "transfer committed but its event callback threw"));
                resultStatus = Status(StatusCode::Applied, std::move(diagnostics));
            } catch (...) {
                resultStatus = Status::success(StatusCode::Applied);
            }
        } catch (...) {
            try {
                std::vector<Diagnostic> diagnostics;
                diagnostics.emplace_back(Diagnostic::warning(DiagnosticCode::CallbackFailure,
                                                             "transfer committed but its event callback threw"));
                resultStatus = Status(StatusCode::Applied, std::move(diagnostics));
            } catch (...) {
                resultStatus = Status::success(StatusCode::Applied);
            }
        }
    }
    return Result<TransferReceipt>::success(std::move(receipt), std::move(resultStatus));
}

}  // namespace eve::container
