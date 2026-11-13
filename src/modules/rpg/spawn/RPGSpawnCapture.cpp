#include "rpg/spawn/RPGSpawnCapture.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include "definitions/Definitions.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillContent.h"
#include "rpg/SkillDefinitionRuntime.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
// Capture phase: View<RPGActor>, read SpawnedActorState and canonical checkpoint components.
// No component writes, structural mutations or event publication; skill content is synchronously observed.
namespace eve::rpg_spawn {
struct RPGSpawnCapture::Impl {
    bool                                     sealing = false;
    PersistentId                             saveEpoch;
    std::vector<CapturedSpawnActor>          actors;
    std::vector<definitions::Definition>     skills;
    std::vector<rpg::EffectDefinition>       effects;
    std::vector<rpg::ImplementationIdentity> implementations;
    std::vector<definitions::Definition>     future;
};
RPGSpawnCapture::RPGSpawnCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGSpawnCapture::~RPGSpawnCapture()                                     = default;
RPGSpawnCapture::RPGSpawnCapture(RPGSpawnCapture&&) noexcept            = default;
RPGSpawnCapture& RPGSpawnCapture::operator=(RPGSpawnCapture&&) noexcept = default;
PersistentId     RPGSpawnCapture::saveEpoch() const noexcept { return impl_ ? impl_->saveEpoch : PersistentId{}; }
std::span<const CapturedSpawnActor> RPGSpawnCapture::actors() const noexcept {
    return impl_ ? std::span<const CapturedSpawnActor>(impl_->actors) : std::span<const CapturedSpawnActor>{};
}
std::span<const definitions::Definition> RPGSpawnCapture::skills() const noexcept {
    return impl_ ? std::span<const definitions::Definition>(impl_->skills) : std::span<const definitions::Definition>{};
}
std::span<const rpg::EffectDefinition> RPGSpawnCapture::effects() const noexcept {
    return impl_ ? std::span<const rpg::EffectDefinition>(impl_->effects) : std::span<const rpg::EffectDefinition>{};
}
std::span<const rpg::ImplementationIdentity> RPGSpawnCapture::implementations() const noexcept {
    return impl_ ? std::span<const rpg::ImplementationIdentity>(impl_->implementations)
                 : std::span<const rpg::ImplementationIdentity>{};
}
std::span<const definitions::Definition> RPGSpawnCapture::futureUnits() const noexcept {
    return impl_ ? std::span<const definitions::Definition>(impl_->future) : std::span<const definitions::Definition>{};
}
Result<RPGSpawnCapture> RPGSpawnCapture::capture(ecs::Table& table, PersistentId saveEpoch) {
    return captureWithFuture(table, saveEpoch, {}, {}, {}, {});
}
Result<RPGSpawnCapture> RPGSpawnCapture::captureWithFuture(
    ecs::Table& table, PersistentId saveEpoch, std::span<const UnitRecipe> futureUnits,
    std::shared_ptr<const rpg::SkillConditionBindings> futureConditions, std::span<const std::string> eventSkills,
    std::span<const std::string> eventEffects) {
    using R   = Result<RPGSpawnCapture>;
    auto fail = [](DiagnosticCode code, std::string message) {
        return R::failure(Diagnostic::error(code, std::move(message), "rpg.spawn.capture"));
    };
    if (saveEpoch.isNil() || table.is_deferred())
        return fail(DiagnosticCode::PreconditionViolation, "Capture requires a joint epoch and a Table outside Views");
    auto frozen       = std::make_unique<Impl>();
    frozen->saveEpoch = saveEpoch;
    std::map<std::string, definitions::Definition> skills;
    std::map<std::string, rpg::EffectDefinition>   effects;
    auto                                           captureEffect = [&](const std::string& effectId) -> Result<void> {
        if (effects.contains(effectId)) return Result<void>::success();
        const auto* effect = rpg::EffectRegistry::find(effectId);
        if (!effect)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Skill references missing runtime effect", effectId));
        auto valid = rpg::validateEffectDefinition(*effect);
        if (!valid) return valid;
        if (effect->id != effectId)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Effect identity differs from its registry key", effectId));
        effects.emplace(effectId, *effect);
        return Result<void>::success();
    };
    auto captureSkill = [&](const std::string& id) -> Result<void> {
        if (skills.contains(id)) return Result<void>::success();
        auto source = rpg::SkillRegistry::captureDefinition(id);
        if (!source) return Result<void>::failure(source.status());
        auto typed = rpg::SkillDefinitionRuntime::prepareCaptured(source.value());
        if (!typed) return Result<void>::failure(typed.status());
        for (const auto& effectId : typed.value().grantedEffects) {
            auto captured = captureEffect(effectId);
            if (!captured) return captured;
        }
        skills.emplace(id, std::move(source).takeValue());
        return Result<void>::success();
    };
    struct ConditionContext {
        std::shared_ptr<const rpg::SkillConditionBindings> bindings;
        std::set<std::string>                              skills;
    };
    std::vector<ConditionContext>    conditionContexts;
    std::set<PersistentId>           subjects;
    std::set<rpg::ActorExecutionKey> executionKeys;
    auto                             view = ecs::View<rpg::RPGActor>(table);
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto& actor  = *it.entity();
        auto* buffer = actor.getComponentManager().getComponentBuffer<SpawnedActorState>();
        if (!buffer || !buffer->has(actor.id)) continue;
        const auto& state      = *buffer->ptr(actor.id);
        const auto  registered = rpg::actorSubject(actor);
        if (!registered) return fail(DiagnosticCode::InvariantViolation, "Spawned actor lacks registered identity");
        const auto subject = *registered;
        if (!subject.isValid() || !subjects.insert(subject.persistentId()).second || !std::isfinite(state.position.x) ||
            !std::isfinite(state.position.y) || !state.pinnedRecipe || !state.pinnedRecipe->member ||
            !state.definition.isValid() || state.pinnedRecipe->canonicalJson.empty() ||
            state.definition != state.pinnedRecipe->definition)
            return fail(DiagnosticCode::InvariantViolation,
                        "Spawned actor has invalid identity, position or provenance");
        if (state.definition.reference.id().namespaceName() != "rpg.unit" ||
            state.pinnedRecipe->member->id != state.definition.reference.id().name() ||
            (state.origin && (state.origin->owner.isNil() || !state.origin->slot.isValid() ||
                              state.origin->epoch == 0 || state.origin->incarnation.isZero())))
            return fail(DiagnosticCode::InvariantViolation, "Spawn actor provenance/origin is invalid");
        auto* orders = actor.getComponentManager().getComponentBuffer<rpg::ActorExecutionOrder>();
        if (!orders || !orders->has(actor.id) || !orders->ptr(actor.id)->key || orders->ptr(actor.id)->key->isNil())
            return fail(DiagnosticCode::PreconditionViolation, "Spawned actor requires a persistent execution key");
        if (!executionKeys.insert(*orders->ptr(actor.id)->key).second)
            return fail(DiagnosticCode::Conflict, "Duplicate actor execution key");
        auto* statusBuffer = actor.getComponentManager().getComponentBuffer<rpg::RPGActor::Statuses>();
        auto* skillBuffer  = actor.getComponentManager().getComponentBuffer<rpg::RPGActor::Skills>();
        if (!statusBuffer || !statusBuffer->has(actor.id) || !skillBuffer || !skillBuffer->has(actor.id))
            return fail(DiagnosticCode::PreconditionViolation,
                        "Capture requires preconfigured status and skill components");
        const auto& statuses     = *statusBuffer->ptr(actor.id);
        const auto& actualSkills = *skillBuffer->ptr(actor.id);
        for (int index = 0; index < statuses.container.effectCount(); ++index) {
            auto captured = captureEffect(statuses.container.effectAt(index)->type);
            if (!captured) return R::failure(captured.status());
        }
        std::optional<rpg::CastTargetBinding> castTarget;
        const auto&                           casting = actualSkills.casting;
        if (casting.active && casting.target.intent == rpg::CastTargetIntent::Linked) {
            const auto& handle = casting.target.entity;
            if (handle.table == &table) {
                auto* target = dynamic_cast<rpg::RPGActor*>(ecs::try_get(handle));
                if (target) {
                    auto*      states        = target->getComponentManager().getComponentBuffer<SpawnedActorState>();
                    const auto targetSubject = rpg::actorSubject(*target);
                    if (!states || !states->has(target->id) || !targetSubject)
                        return fail(DiagnosticCode::Unsupported,
                                    "Live cast target is outside the spawned actor participant");
                    castTarget = rpg::CastTargetBinding{*targetSubject, handle};
                }
            }
        }
        auto checkpoint = actor.checkpointJson(castTarget);
        if (!checkpoint) return R::failure(checkpoint.status());
        ConditionContext context{actualSkills.conditionBindings, {state.pinnedRecipe->member->skillId}};
        for (const auto& [id, runtime] : actualSkills.known) {
            context.skills.insert(id);
            (void)runtime;
            auto captured = captureSkill(id);
            if (!captured) return R::failure(captured.status());
        }
        auto originalSkill = captureSkill(state.pinnedRecipe->member->skillId);
        if (!originalSkill) return R::failure(originalSkill.status());
        conditionContexts.push_back(std::move(context));
        frozen->actors.push_back(
            {subject, state.origin, state.position, state.pinnedRecipe, std::move(checkpoint).takeValue()});
    }
    std::map<std::string, definitions::Definition> future;
    for (const auto& recipe : futureUnits) {
        if (!recipe.definition.isValid() || recipe.canonicalJson.empty())
            return fail(DiagnosticCode::InvalidArgument, "Future recipe requires exact valid provenance and source");
        definitions::Definition record{std::string(recipe.definition.reference.id().namespaceName()),
                                       std::string(recipe.definition.reference.id().name()), SchemaVersion(1),
                                       recipe.definition.generation, recipe.canonicalJson};
        auto                    admitted = RPGUnitCatalogue::prepareCaptured(record);
        if (!admitted) return R::failure(admitted.status());
        auto skill = captureSkill(admitted.value().member->skillId);
        if (!skill) return R::failure(skill.status());
        conditionContexts.push_back({futureConditions, {admitted.value().member->skillId}});
        if (!future.emplace(recipe.definition.reference.format(), std::move(record)).second)
            return fail(DiagnosticCode::Conflict, "Duplicate future unit binding");
    }
    // Pending historical events remain content participants even after their actors/effects finish.
    for (const auto& id : eventSkills) {
        auto captured = captureSkill(id);
        if (!captured) return R::failure(captured.status());
        conditionContexts.push_back({futureConditions, {id}});
    }
    for (const auto& id : eventEffects) {
        auto captured = captureEffect(id);
        if (!captured) return R::failure(captured.status());
    }
    frozen->future.reserve(future.size());
    for (auto& [key, record] : future) {
        (void)key;
        frozen->future.push_back(std::move(record));
    }
    std::sort(frozen->actors.begin(), frozen->actors.end(),
              [](const auto& a, const auto& b) { return a.subject.persistentId() < b.subject.persistentId(); });
    frozen->skills.reserve(skills.size());
    for (auto& [id, record] : skills) {
        (void)id;
        frozen->skills.push_back(std::move(record));
    }
    frozen->effects.reserve(effects.size());
    for (auto& [id, effect] : effects) {
        (void)id;
        frozen->effects.push_back(std::move(effect));
    }
    auto content = rpg::SkillContent::prepareWithEffects(frozen->skills, frozen->effects);
    if (!content) return R::failure(content.status());
    for (const auto& requirement : content.value().implementationRequirements()) {
        if (requirement.kind != rpg::ImplementationKind::ConditionPolicy) {
            auto revision = rpg::currentImplementationRevision(requirement.kind, requirement.name);
            if (!revision) return R::failure(revision.status());
            frozen->implementations.push_back({requirement.kind, requirement.name, std::move(revision).takeValue()});
            continue;
        }
        std::optional<std::string> revision;
        for (const auto& context : conditionContexts) {
            bool required = false;
            for (const auto& use : requirement.uses) {
                if (!context.skills.contains(use.sourceId)) continue;
                required = true;
                if (!context.bindings)
                    return fail(DiagnosticCode::Unsupported,
                                "Condition policy requires its actual actor binding: " + requirement.name);
                auto declared = context.bindings->validateDeclaration(requirement.name, use.declaration, use.arguments);
                if (!declared) return R::failure(declared.status());
            }
            if (!required) continue;
            auto actual =
                rpg::currentImplementationRevision(requirement.kind, requirement.name, context.bindings.get());
            if (!actual) return R::failure(actual.status());
            if (revision && *revision != actual.value())
                return fail(DiagnosticCode::Conflict,
                            "One archived policy name has conflicting actual actor revisions: " + requirement.name);
            revision = std::move(actual).takeValue();
        }
        if (!revision)
            return fail(DiagnosticCode::InvariantViolation, "Condition requirement has no owning source use");
        frozen->implementations.push_back({requirement.kind, requirement.name, std::move(*revision)});
    }
    return R::success(RPGSpawnCapture(std::move(frozen)));
}
}  // namespace eve::rpg_spawn

#include "rpg/spawn/RPGSpawnArchiveCodec.inl"
