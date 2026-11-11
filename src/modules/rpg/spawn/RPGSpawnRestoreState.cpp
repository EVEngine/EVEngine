#include "rpg/spawn/RPGSpawnRestoreState.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include "definitions/Definitions.h"
#include "rpg/ActorInitialization.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/SkillContent.h"
#include "rpg/spawn/RPGSpawnArchive.h"
#include "rpg/spawn/RPGSpawnRestoreStateInternal.inl"
#include "rpg/spawn/RPGUnitCatalogue.h"
namespace eve::rpg_spawn {
RPGSpawnRestoreState::RPGSpawnRestoreState(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGSpawnRestoreState::~RPGSpawnRestoreState()                                          = default;
RPGSpawnRestoreState::RPGSpawnRestoreState(RPGSpawnRestoreState&&) noexcept            = default;
RPGSpawnRestoreState& RPGSpawnRestoreState::operator=(RPGSpawnRestoreState&&) noexcept = default;
PersistentId          RPGSpawnRestoreState::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
Revision              RPGSpawnRestoreState::revision() const noexcept { return impl_ ? impl_->revision : Revision{}; }
SimulationTick        RPGSpawnRestoreState::tick() const noexcept { return impl_ ? impl_->tick : SimulationTick{}; }
std::size_t           RPGSpawnRestoreState::actorCount() const noexcept { return impl_ ? impl_->actors.size() : 0; }
std::size_t RPGSpawnRestoreState::unitRevisionCount() const noexcept { return impl_ ? impl_->units.size() : 0; }
std::size_t RPGSpawnRestoreState::futureUnitCount() const noexcept { return impl_ ? impl_->future.size() : 0; }
Result<std::shared_ptr<const UnitRecipe>> RPGSpawnRestoreState::futureUnit(const DefinitionRef& reference) const {
    if (!impl_)
        return Result<std::shared_ptr<const UnitRecipe>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Restore state consumed"));
    auto found = impl_->future.find(reference.format());
    if (found == impl_->future.end())
        return Result<std::shared_ptr<const UnitRecipe>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Explicit future unit binding is absent", reference.format()));
    return Result<std::shared_ptr<const UnitRecipe>>::success(found->second);
}
std::span<const rpg::ImplementationRequirement> RPGSpawnRestoreState::implementationRequirements() const noexcept {
    return impl_ ? impl_->skills->implementationRequirements() : std::span<const rpg::ImplementationRequirement>{};
}
std::shared_ptr<const rpg::SkillConditionBindings> RPGSpawnRestoreState::conditionOwner() const noexcept {
    return impl_ ? impl_->conditions : nullptr;
}
Result<rpg::PreparedMemberActor> RPGSpawnRestoreState::prepareFutureUnit(const DefinitionRef& reference) const {
    auto recipe = futureUnit(reference);
    if (!recipe) return Result<rpg::PreparedMemberActor>::failure(recipe.status());
    return rpg::PreparedMemberActor::prepareCaptured(*recipe.value()->member, impl_->skills, impl_->conditions);
}
Result<void> RPGSpawnRestoreState::validateImplementationBindings() const {
    if (!impl_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Restore state consumed"));
    return impl_->skills->validateImplementationBindings(impl_->conditions.get());
}
std::shared_ptr<const rpg::SkillContent> RPGSpawnRestoreState::contentOwner() const noexcept {
    return impl_ ? impl_->skills : nullptr;
}
Result<void> RPGSpawnRestoreState::validateEventClosure(std::span<const SubjectRef>  subjects,
                                                        std::span<const std::string> skills,
                                                        std::span<const std::string> effects) const {
    if (!impl_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Restore state consumed"));
    std::unordered_set<SubjectRef> actors;
    actors.reserve(impl_->actors.size());
    for (const auto& actor : impl_->actors) actors.insert(actor.subject);
    for (auto subject : subjects)
        if (!actors.contains(subject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Event actor outside saved graph", subject.format()));
    for (const auto& id : skills) {
        auto admitted = impl_->skills->definition(id);
        if (!admitted) return Result<void>::failure(admitted.status());
    }
    for (const auto& id : effects) {
        auto admitted = impl_->skills->effect(id);
        if (!admitted) return Result<void>::failure(admitted.status());
    }
    return Result<void>::success();
}
Result<RPGSpawnRestoreState> RPGSpawnRestoreState::prepare(
    const RPGSpawnArchive& archive, std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R = Result<RPGSpawnRestoreState>;
    if (archive.saveEpoch().isNil())
        return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Archive consumed"));
    auto skills = rpg::SkillContent::prepareWithEffects(archive.skills(), archive.effects(), archive.implementations());
    if (!skills) return R::failure(skills.status());
    auto closed = skills.value().implementationClosure();
    if (!closed) return R::failure(closed.status());
    auto state        = std::make_unique<Impl>();
    state->conditions = std::move(conditions);
    state->epoch      = archive.saveEpoch();
    state->revision   = archive.revision();
    state->tick       = archive.tick();
    state->skills     = std::make_shared<const rpg::SkillContent>(std::move(skills).takeValue());
    state->units.reserve(archive.units().size());
    for (const auto& record : archive.units()) {
        auto recipe = RPGUnitCatalogue::prepareCaptured(record, *state->skills);
        if (!recipe) return R::failure(recipe.status());
        state->units.push_back(std::make_shared<const UnitRecipe>(std::move(recipe).takeValue()));
    }
    for (auto index : archive.futureUnitRevisions()) {
        const auto& recipe = state->units[index];
        if (!state->future.emplace(recipe->definition.reference.format(), recipe).second)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Duplicate canonical future unit binding"));
    }
    state->actors.reserve(archive.actors().size());
    for (const auto& actor : archive.actors()) {
        const auto& recipe = *state->units[actor.unitRevision];
        auto candidate = rpg::PreparedMemberActor::prepareCaptured(*recipe.member, state->skills, state->conditions);
        if (!candidate) return R::failure(candidate.status());
        auto restored = candidate.value().restoreCheckpoint(
            actor.checkpointJson, rpg::ActorExecutionKey::fromUuid(actor.subject.persistentId()));
        if (!restored) return R::failure(restored.status());
        state->actors.push_back(
            {actor.subject, actor.origin, actor.position, actor.unitRevision, std::move(candidate).takeValue()});
    }
    std::set<PersistentId>           subjects;
    std::set<rpg::ActorExecutionKey> executionKeys;
    for (const auto& actor : state->actors) subjects.insert(actor.subject.persistentId());
    for (const auto& actor : state->actors) {
        auto key = actor.state.executionKey();
        if (!key) return R::failure(key.status());
        if (!executionKeys.insert(key.value()).second)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Duplicate archived actor execution key"));
        auto target = actor.state.restoredCastTarget();
        if (!target) return R::failure(target.status());
        if (target.value() && !subjects.contains(target.value()->persistentId()))
            return R::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "Persistent cast target is absent from the complete actor participant"));
    }
    return R::success(RPGSpawnRestoreState(std::move(state)));
}
}  // namespace eve::rpg_spawn

#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include "common/Assert.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "spawn/SpawnRestoreState.h"
#include "spawn/SpawnWorld.h"
namespace eve::rpg_spawn {
namespace {
class ArchivedRestoreUnits final : public spawn::IUnitDefinitionResolver {
    const RPGSpawnRestoreState& domain_;

public:
    explicit ArchivedRestoreUnits(const RPGSpawnRestoreState& domain) : domain_(domain) {}
    [[nodiscard]] Result<void> validate(const DefinitionRef& reference) const override {
        auto recipe = domain_.futureUnit(reference);
        if (!recipe) return Result<void>::failure(recipe.status());
        return Result<void>::success();
    }
};
}  // namespace
struct RPGSpawnJointRestore::Impl {
    PersistentId                                epoch;
    spawn::SpawnRestoreState                    core;
    std::shared_ptr<const RPGSpawnRestoreState> domain;
    std::vector<spawn::SavedLifecycleInput>     pending;
    Impl(spawn::SpawnRestoreState coreValue, RPGSpawnRestoreState domainValue,
         std::vector<spawn::SavedLifecycleInput> pendingValue)
        : epoch(coreValue.saveEpoch()),
          core(std::move(coreValue)),
          domain(std::make_shared<const RPGSpawnRestoreState>(std::move(domainValue))),
          pending(std::move(pendingValue)) {}
};
RPGSpawnJointRestore::RPGSpawnJointRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGSpawnJointRestore::~RPGSpawnJointRestore()                                                    = default;
RPGSpawnJointRestore::RPGSpawnJointRestore(RPGSpawnJointRestore&&) noexcept                      = default;
RPGSpawnJointRestore&           RPGSpawnJointRestore::operator=(RPGSpawnJointRestore&&) noexcept = default;
const spawn::SpawnRestoreState& RPGSpawnJointRestore::core() const noexcept {
    if (!impl_) EV_PARAM_CHECK(false, "Joined restore was consumed");
    return impl_->core;
}
const RPGSpawnRestoreState& RPGSpawnJointRestore::domain() const noexcept {
    if (!impl_) EV_PARAM_CHECK(false, "Joined restore was consumed");
    return *impl_->domain;
}
std::shared_ptr<const RPGSpawnRestoreState> RPGSpawnJointRestore::domainOwner() const noexcept {
    return impl_ ? impl_->domain : nullptr;
}
PersistentId RPGSpawnJointRestore::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
Result<spawn::SpawnWorld> RPGSpawnJointRestore::bindCore(const std::shared_ptr<spawn::SpawnProviders>& providers) {
    if (!impl_)
        return Result<spawn::SpawnWorld>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Joint candidate consumed"));
    return spawn::SpawnWorld::restore(impl_->core, providers);
}
std::span<const spawn::SavedLifecycleInput> RPGSpawnJointRestore::pendingLifecycle() const noexcept {
    return impl_ ? std::span<const spawn::SavedLifecycleInput>(impl_->pending)
                 : std::span<const spawn::SavedLifecycleInput>{};
}
Result<void> RPGSpawnRestoreState::validateRelations(std::span<const spawn::SavedSpawnMember>    membership,
                                                     std::uint64_t                               acceptedSequence,
                                                     std::span<const spawn::SavedLifecycleInput> pending) const {
    using R   = Result<void>;
    auto fail = [](std::string message) {
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move(message), "rpg.spawn.joint-restore"));
    };
    if (!impl_) return fail("Restore state consumed");
    if (pending.size() > membership.size()) return fail("More pending removals than tracked subjects");
    auto member = [&](PersistentId id) {
        auto found =
            std::lower_bound(membership.begin(), membership.end(), id,
                             [](const auto& entry, PersistentId key) { return entry.subject.persistentId() < key; });
        return found != membership.end() && found->subject.persistentId() == id ? found : membership.end();
    };
    // @lifetime These local indices borrow only until all validation ends; no callback, mutation or publication occurs.
    std::map<PersistentId, const RPGSpawnRestoreState::Impl::Actor*> actors;
    std::map<PersistentId, const spawn::SavedLifecycleInput*>        retired;
    for (const auto& actor : impl_->actors) actors.emplace(actor.subject.persistentId(), &actor);
    auto sequence = acceptedSequence;
    for (const auto& input : pending) {
        switch (input.reason) {
            case spawn::RemovalReason::Death:
            case spawn::RemovalReason::Administrative:
            case spawn::RemovalReason::Unloaded:
            case spawn::RemovalReason::Detached: break;
            default: return fail("Unknown pending removal reason");
        }
        const auto found = member(input.subject.persistentId());
        if (!input.subject.isValid() || input.sequence <= sequence || found == membership.end() ||
            found->source != input.source || !retired.emplace(input.subject.persistentId(), &input).second)
            return fail("Pending removal sequence, subject or source does not match admitted core");
        sequence = input.sequence;
    }
    for (const auto& entry : membership) {
        auto       found          = actors.find(entry.subject.persistentId());
        const bool pendingRemoval = retired.contains(entry.subject.persistentId());
        if (found == actors.end()) {
            if (!pendingRemoval) return fail("Tracked subject is missing without a pending removal");
            continue;
        }
        const auto& actor = *found->second;
        if (pendingRemoval) {
            if (actor.origin) return fail("Pending domain removal still has a live origin projection");
        } else {
            if (!actor.origin || *actor.origin != entry.source) return fail("Core member and domain origin disagree");
            auto hp = actor.state.currentVital("hp");
            if (!hp) return R::failure(hp.status());
            if (hp.value() <= 0) return fail("Source-owned actor is dead without a pending lifecycle fact");
        }
    }
    for (const auto& actor : impl_->actors) {
        if (!actor.origin) continue;
        const auto found = member(actor.subject.persistentId());
        if (found == membership.end() || found->source != *actor.origin)
            return fail("Domain origin has no matching admitted core membership");
    }
    return R::success();
}
Result<RPGSpawnJointRestore> RPGSpawnJointRestore::prepare(
    const SnapshotEnvelope& coreInput, const SnapshotEnvelope& domainInput, PersistentId saveEpoch,
    const SnapshotHashProvider& hash, std::span<const spawn::SavedLifecycleInput> pendingInput,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R   = Result<RPGSpawnJointRestore>;
    auto fail = [](std::string message) {
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move(message), "rpg.spawn.joint-restore"));
    };
    auto                                    coreEnvelope   = coreInput;
    auto                                    domainEnvelope = domainInput;
    std::vector<spawn::SavedLifecycleInput> pending(pendingInput.begin(), pendingInput.end());
    auto                                    archive = RPGSpawnArchive::parse(domainEnvelope, saveEpoch, hash);
    if (!archive) return R::failure(archive.status());
    auto domainResult = RPGSpawnRestoreState::prepare(archive.value(), std::move(conditions));
    if (!domainResult) return R::failure(domainResult.status());
    auto                 domain = std::move(domainResult).takeValue();
    ArchivedRestoreUnits units(domain);
    auto                 coreResult = spawn::SpawnRestoreState::decode(coreEnvelope, saveEpoch, hash, units);
    if (!coreResult) return R::failure(coreResult.status());
    auto core = std::move(coreResult).takeValue();
    if (core.saveEpoch() != domain.saveEpoch() || core.revision() != domain.revision() || core.tick() != domain.tick())
        return fail("Participant save epoch, revision or simulation tick disagree");
    auto relations = domain.validateRelations(core.membership(), core.acceptedLifecycleSequence(), pending);
    if (!relations) return R::failure(relations.status());
    return R::success(
        RPGSpawnJointRestore(std::make_unique<Impl>(std::move(core), std::move(domain), std::move(pending))));
}
}  // namespace eve::rpg_spawn

#include "rpg/spawn/RPGSpawnBundleCodec.inl"
