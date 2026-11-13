#include <cmath>
#include <map>
#include <set>
#include "rts/RTS.h"
#include "rts/spawn/RTSSpawnCapture.h"
namespace eve::rts_spawn {
Result<void> RTSSpawnCapture::validateRelations(std::span<const spawn::SavedSpawnMember>    membership,
                                                std::uint64_t                               acceptedSequence,
                                                std::span<const rts::RTSUnitSnapshot>       roots,
                                                std::span<const spawn::SavedLifecycleInput> pending,
                                                std::span<const rts::RTSFrameDamageEvent>   damage) const {
    auto reject = [](const char* message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, message));
    };
    if (saveEpoch().isNil() || acceptedSequence > lifecycleSequence())
        return reject("Consumed capture or lifecycle watermark mismatch");
    // @lifetime Local read-only indices borrow inputs only through this synchronous, callback-free admission.
    std::map<PersistentId, spawn::SavedSpawnerKey>      tracked;
    std::map<PersistentId, const rts::RTSUnitSnapshot*> bodies;
    std::map<PersistentId, const CapturedSpawnUnit*>    admitted;
    for (const auto& member : membership) {
        if (!member.subject.isValid() || member.source.owner != sourceOwner() || member.source.epoch != sourceEpoch() ||
            !tracked.emplace(member.subject.persistentId(), member.source).second)
            return reject("Invalid/duplicate core subject or source scope");
    }
    for (const auto& root : roots)
        if (!root.subject.isValid() || !bodies.emplace(root.subject.persistentId(), &root).second)
            return reject("Invalid/duplicate native Unit subject");
    for (const auto& member : units()) {
        if (!member.subject.isValid() || !admitted.emplace(member.subject.persistentId(), &member).second)
            return reject("Duplicate admitted subject");
        if (bodies.contains(member.subject.persistentId()) != (member.presence == CapturedUnitPresence::Present))
            return reject("Native root and admitted presence disagree");
        if (member.origin) {
            auto found = tracked.find(member.subject.persistentId());
            if (found == tracked.end() || found->second != spawn::savedSpawnerKey(*member.origin))
                return reject("Admitted origin is absent from core");
        }
    }
    std::map<PersistentId, spawn::RemovalReason> retired;
    auto                                         previous = acceptedSequence;
    for (const auto& input : pending) {
        switch (input.reason) {
            case spawn::RemovalReason::Death:
            case spawn::RemovalReason::Administrative:
            case spawn::RemovalReason::Unloaded:
            case spawn::RemovalReason::Detached: break;
            default: return reject("Unknown pending removal reason");
        }
        auto member = tracked.find(input.subject.persistentId());
        if (!input.subject.isValid() || input.sequence <= previous || input.sequence > lifecycleSequence() ||
            member == tracked.end() || member->second != input.source ||
            !retired.emplace(input.subject.persistentId(), input.reason).second)
            return reject("Pending lifecycle source or sequence mismatch");
        previous    = input.sequence;
        auto origin = admitted.find(input.subject.persistentId());
        if (origin != admitted.end() && origin->second->origin)
            return reject("Translated removal still owns an origin");
    }
    if (previous != lifecycleSequence()) return reject("Translated lifecycle watermark lacks retained facts");
    std::set<PersistentId> deaths;
    for (const auto& input : damage) {
        const auto& outcome = input.outcome;
        if (std::isfinite(outcome.previousHealth) && std::isfinite(outcome.health) && outcome.previousHealth > 0 &&
            outcome.health <= 0 && outcome.target.isValid() && input.request.target == outcome.target &&
            input.tick == input.request.tick)
            deaths.insert(outcome.target.persistentId());
    }
    for (const auto& [id, source] : tracked) {
        auto       body = bodies.find(id);
        const bool alive =
            body != bodies.end() && body->second->durability.alive && body->second->durability.state.health > 0;
        if (auto pendingRemoval = retired.find(id); pendingRemoval != retired.end()) {
            if (alive || (pendingRemoval->second != spawn::RemovalReason::Death && body != bodies.end()))
                return reject("Pending removal contradicts native body");
            continue;
        }
        auto member = admitted.find(id);
        if (member == admitted.end() || !member->second->origin ||
            spawn::savedSpawnerKey(*member->second->origin) != source)
            return reject("Core member lacks admitted origin");
        if (!alive && !deaths.contains(id)) return reject("Dead/missing member lacks retained damage handoff");
    }
    return Result<void>::success();
}
}  // namespace eve::rts_spawn
