#include "rpg/ActorInitialization.h"
#include "rpg/CastingPersistenceInternal.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillContent.h"

#include <cmath>

namespace eve::rpg {
void initializeActorComponents(RPGActor& actor) {
    actor.attributes().emplace();
    actor.statuses().emplace();
    actor.skills().emplace();
    actor.progression().emplace();
    actor.vitals().emplace();
    actor.traits().emplace();
    actor.classInfo().emplace();
    actor.executionOrder().emplace();
    actor.identity().emplace();
}
namespace {
Result<void> validateMember(const EncounterMemberDefinition& member, const SkillContent* skills) {
    auto stableId = [](const std::string& id) {
        if (id.empty() || id.size() > 256) return false;
        for (const unsigned char c : id)
            if (c < 0x20 || c == 0x7f) return false;
        return true;
    };
    if (!stableId(member.id) || member.displayName.empty() || member.displayName.size() > 512 ||
        !stableId(member.skillId) || !std::isfinite(member.attack) || member.attack < 0 ||
        !std::isfinite(member.defense) || member.defense < 0 || !std::isfinite(member.maxHp) || member.maxHp <= 0 ||
        !std::isfinite(member.speed) || member.speed < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RPG member recipe", "rpg.unit"));
    if (skills ? !skills->contains(member.skillId) : SkillRegistry::find(member.skillId) == nullptr)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Unit skill does not exist", member.skillId));
    return Result<void>::success();
}
}  // namespace
Result<void> validateMemberDefinition(const EncounterMemberDefinition& member) {
    return validateMember(member, nullptr);
}
Result<void> validateCapturedMemberDefinition(const EncounterMemberDefinition& member, const SkillContent& skills) {
    return validateMember(member, &skills);
}
struct PreparedMemberActor::Impl {
    std::shared_ptr<const SkillContent> skills;
    RPGActor::CheckpointCandidate       state;
    explicit Impl(RPGActor::CheckpointCandidate value) : state(std::move(value)) {}
};
PreparedMemberActor::PreparedMemberActor(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedMemberActor::~PreparedMemberActor()                                                = default;
PreparedMemberActor::PreparedMemberActor(PreparedMemberActor&&) noexcept                   = default;
PreparedMemberActor&        PreparedMemberActor::operator=(PreparedMemberActor&&) noexcept = default;
Result<PreparedMemberActor> PreparedMemberActor::prepare(const EncounterMemberDefinition&              member,
                                                         std::shared_ptr<const SkillConditionBindings> conditions) {
    auto candidate = prepareWithContent(member, {});
    if (!candidate) return candidate;
    if (conditions) {
        const auto* skill = SkillRegistry::find(member.skillId);
        auto        bound = validateSkillConditionBindings(skill->castCondition, conditions.get());
        if (!bound) return Result<PreparedMemberActor>::failure(bound.status());
    }
    candidate.value().impl_->state.skills.conditionBindings = std::move(conditions);
    return candidate;
}
Result<PreparedMemberActor> PreparedMemberActor::prepareCaptured(
    const EncounterMemberDefinition& member, std::shared_ptr<const SkillContent> skills,
    std::shared_ptr<const SkillConditionBindings> conditions) {
    if (!skills)
        return Result<PreparedMemberActor>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Skill archive absent"));
    auto candidate = prepareWithContent(member, std::move(skills));
    if (candidate) candidate.value().impl_->state.skills.conditionBindings = std::move(conditions);
    return candidate;
}
Result<PreparedMemberActor> PreparedMemberActor::prepareWithContent(const EncounterMemberDefinition&    member,
                                                                    std::shared_ptr<const SkillContent> skills) {
    auto valid = validateMember(member, skills.get());
    if (!valid) return Result<PreparedMemberActor>::failure(valid.status());
    auto attributes = ::eve::attributes::AttributeSet::prepareEmpty();
    attributes.setBase("attack", member.attack);
    attributes.setBase("defense", member.defense);
    attributes.setBase("hp", member.maxHp);
    attributes.setBase("speed", member.speed);
    RPGActor::CheckpointCandidate state{std::move(attributes), {}, {}, {}, {}};
    state.skills.known.emplace(member.skillId, SkillRuntime{});
    state.vitals.current.emplace("hp", member.maxHp);
    auto impl    = std::make_unique<Impl>(std::move(state));
    impl->skills = std::move(skills);
    return Result<PreparedMemberActor>::success(PreparedMemberActor(std::move(impl)));
}
Result<PreparedMemberActor> PreparedMemberActor::clone() const {
    if (!impl_)
        return Result<PreparedMemberActor>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Actor candidate consumed"));
    const auto&                   original = impl_->state;
    RPGActor::CheckpointCandidate state{original.attributes.view().prepareCopy(),
                                        original.skills,
                                        original.progression,
                                        original.vitals,
                                        original.classInfo,
                                        original.statuses,
                                        original.castTarget,
                                        original.executionOrder,
                                        original.sourceVersion};
    auto                          impl = std::make_unique<Impl>(std::move(state));
    impl->skills                       = impl_->skills;
    return Result<PreparedMemberActor>::success(PreparedMemberActor(std::move(impl)));
}
Result<void> PreparedMemberActor::restoreCheckpoint(std::string_view                 json,
                                                    std::optional<ActorExecutionKey> legacyOrder) {
    if (!impl_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Actor candidate consumed"));
    auto state = RPGActor::prepareCheckpointState(json, impl_->state.attributes.view(), impl_->state.statuses, {},
                                                  impl_->state.classInfo, impl_->skills.get());
    if (!state) return Result<void>::failure(state.status());
    if (state.value().sourceVersion < 5) state.value().executionOrder = impl_->state.executionOrder;
    if (legacyOrder) {
        if (legacyOrder->isNil())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Legacy execution order key must be non-nil"));
        if (state.value().sourceVersion < 5) state.value().executionOrder.key = legacyOrder;
    }
    state.value().skills.conditionBindings = impl_->state.skills.conditionBindings;
    impl_->state                           = std::move(state).takeValue();
    return Result<void>::success();
}
Result<ActorExecutionKey> PreparedMemberActor::executionKey() const {
    if (!impl_)
        return Result<ActorExecutionKey>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Actor candidate consumed"));
    if (!impl_->state.executionOrder.key || impl_->state.executionOrder.key->isNil())
        return Result<ActorExecutionKey>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Actor has no execution key"));
    return Result<ActorExecutionKey>::success(*impl_->state.executionOrder.key);
}
Result<std::optional<SubjectRef>> PreparedMemberActor::restoredCastTarget() const {
    if (!impl_)
        return Result<std::optional<SubjectRef>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Actor candidate consumed"));
    return Result<std::optional<SubjectRef>>::success(impl_->state.castTarget);
}
Result<void> PreparedMemberActor::publishTo(RPGActor& actor, std::optional<CastTargetBinding> target) {
    if (!impl_ || !actor.table || actor.table->is_deferred())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Actor publication requires a live candidate outside Views"));
    auto& manager = actor.getComponentManager();
    auto  present = [&]<class T>() {
        const auto* b = manager.getComponentBuffer<T>();
        return b && b->has(actor.id);
    };
    if (!present.template operator()<RPGActor::Attributes>() || !present.template operator()<RPGActor::Statuses>() ||
        !present.template operator()<RPGActor::Skills>() || !present.template operator()<RPGActor::Progression>() ||
        !present.template operator()<RPGActor::Vitals>() || !present.template operator()<RPGActor::Traits>() ||
        !present.template operator()<RPGActor::ClassInfo>() || !present.template operator()<ActorExecutionOrder>() ||
        actor.statuses()->container.effectCount() != 0 || actor.skills()->casting.active ||
        !actor.traits()->active.empty() || !actor.classInfo()->classId.empty() ||
        actor.classInfo()->skillsSyncedUpTo != 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Actor publication requires clean preconfigured components"));
    auto valid = validateCreationBindings();
    if (!valid) return valid;
    // Resolve into local data first: even target rejection cannot alter the retryable candidate.
    auto casting = impl_->state.skills.casting;
    auto bound   = bindCastingState(casting, impl_->state.castTarget, actor, target);
    if (!bound) return bound;
    impl_->state.skills.casting = std::move(casting);
    applyTo(actor);
    return Result<void>::success();
}
void PreparedMemberActor::applyTo(RPGActor& actor) noexcept {
    actor.commitCheckpoint(std::move(impl_->state));
    impl_.reset();
}
Result<double> PreparedMemberActor::currentVital(std::string_view resource) const {
    if (!impl_)
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Actor candidate consumed"));
    const auto found = impl_->state.vitals.current.find(std::string(resource));
    if (found == impl_->state.vitals.current.end())
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Admitted vital is absent", std::string(resource)));
    return Result<double>::success(found->second);
}
Result<void> PreparedMemberActor::validateCreationBindings() const {
    if (!impl_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Actor candidate consumed"));
    if (impl_->skills) {
        auto bindings = impl_->skills->validateImplementationBindings(impl_->state.skills.conditionBindings.get());
        if (!bindings) return Result<void>::failure(bindings.status());
    }
    auto validateEffect = [&](const std::string& id) -> Result<void> {
        if (!impl_->skills) {
            if (!EffectRegistry::find(id))
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "Prepared actor effect content expired", id));
            return Result<void>::success();
        }
        auto frozen = impl_->skills->effect(id);
        if (!frozen) return Result<void>::failure(frozen.status());
        const auto* actualEffect = EffectRegistry::find(id);
        if (!actualEffect)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Admitted effect is absent from runtime", id));
        auto expectedFields = encodeEffectDefinition(frozen.value().get());
        if (!expectedFields) return Result<void>::failure(expectedFields.status());
        auto actualFields = encodeEffectDefinition(*actualEffect);
        if (!actualFields) return Result<void>::failure(actualFields.status());
        auto expectedBytes = expectedFields.value().toJson();
        if (!expectedBytes) return Result<void>::failure(expectedBytes.status());
        auto actualBytes = actualFields.value().toJson();
        if (!actualBytes) return Result<void>::failure(actualBytes.status());
        if (expectedBytes.value() != actualBytes.value())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Live effect differs from admitted archive", id));
        return Result<void>::success();
    };
    for (const auto& [id, runtime] : impl_->state.skills.known) {
        (void)runtime;
        if (impl_->skills) {
            auto expected = impl_->skills->source(id);
            if (!expected) return Result<void>::failure(expected.status());
            auto actual = SkillRegistry::captureDefinition(id);
            if (!actual) return Result<void>::failure(actual.status());
            const auto& source = expected.value().get();
            if (actual.value().generation != source.generation || actual.value().version != source.version ||
                actual.value().json != source.json)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                               "Live skill content differs from admitted archive", id));
            auto definition = impl_->skills->definition(id);
            if (!definition) return Result<void>::failure(definition.status());
            for (const auto& effectId : definition.value().get().grantedEffects) {
                auto valid = validateEffect(effectId);
                if (!valid) return valid;
            }
        }
        const auto* actualSkill = SkillRegistry::find(id);
        if (!actualSkill)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Prepared actor skill content expired", id));
        if (!impl_->skills && impl_->state.skills.conditionBindings) {
            auto bound =
                validateSkillConditionBindings(actualSkill->castCondition, impl_->state.skills.conditionBindings.get());
            if (!bound) return Result<void>::failure(bound.status());
        }
    }
    for (int i = 0; i < impl_->state.statuses.container.effectCount(); ++i) {
        auto valid = validateEffect(impl_->state.statuses.container.effectAt(i)->type);
        if (!valid) return valid;
    }
    return Result<void>::success();
}
Result<ecs::EntityHandle> PreparedMemberActor::create() {
    if (!impl_ || ecs::current()->is_deferred())
        return Result<ecs::EntityHandle>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Actor creation requires an unconsumed candidate outside Views"));
    auto bindings = validateCreationBindings();
    if (!bindings) return Result<ecs::EntityHandle>::failure(bindings.status());
    if (impl_->state.castTarget)
        return Result<ecs::EntityHandle>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Linked cast restoration requires a complete candidate-world target binding"));
    RPGActor* actor = nullptr;
    try {
        actor      = RPGActor::createActor();
        auto bound = bindCastingState(impl_->state.skills.casting, impl_->state.castTarget, *actor, std::nullopt);
        if (!bound) {
            ecs::DestroyEntity(actor);
            actor = nullptr;
            return Result<ecs::EntityHandle>::failure(bound.status());
        }
        applyTo(*actor);
        return Result<ecs::EntityHandle>::success(ecs::handle_of(actor));
    } catch (const std::exception& error) {
        if (actor) ecs::DestroyEntity(actor);
        return Result<ecs::EntityHandle>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "rpg.actor.create"));
    } catch (...) {
        if (actor) ecs::DestroyEntity(actor);
        return Result<ecs::EntityHandle>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Actor creation failed", "rpg.actor.create"));
    }
}
Result<void> initializeMemberActor(RPGActor& actor, const EncounterMemberDefinition& member) {
    if (!actor.table || actor.table->is_deferred())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Member initialization requires an ECS owner outside Views"));
    auto prepared = PreparedMemberActor::prepare(member);
    if (!prepared) return Result<void>::failure(prepared.status());
    initializeActorComponents(actor);
    prepared.value().applyTo(actor);
    return Result<void>::success();
}
Result<ecs::EntityHandle> createInitializedActor(const EncounterMemberDefinition& member) {
    try {
        auto prepared = PreparedMemberActor::prepare(member);
        if (!prepared) return Result<ecs::EntityHandle>::failure(prepared.status());
        return prepared.value().create();
    } catch (const std::exception& error) {
        return Result<ecs::EntityHandle>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "rpg.actor.prepare"));
    } catch (...) {
        return Result<ecs::EntityHandle>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Actor preparation failed", "rpg.actor.prepare"));
    }
}
}  // namespace eve::rpg
