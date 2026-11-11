#include "weapon/WeaponRestore.h"
#include <array>
#include <type_traits>
#include "attributes/AttributeProjectionCapture.h"
namespace eve::weapon {
namespace {
template <class T>
T* existing(WeaponEntity& entity) noexcept {
    auto* buffer = entity.getComponentManager().template getComponentBuffer<T>();
    return buffer && buffer->has(entity.id) ? buffer->ptr(entity.id) : nullptr;
}
bool sameHandle(ecs::EntityHandle a, ecs::EntityHandle b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
Result<void> poolMatches(const AmmoPoolLink& link, const CapturedAmmoPool& saved, ecs::Table& world) {
    auto* pool = link.resolve();
    if (!pool || link.handle().table != &world)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Restored weapon pool retired or changed world"));
    const auto* identity = pool->getComponentManager().getComponentBuffer<AmmoPoolEntity::Identity>()->ptr(pool->id);
    const auto* state    = pool->getComponentManager().getComponentBuffer<AmmoPoolEntity::State>()->ptr(pool->id);
    if (identity->id != saved.identity.id || identity->ammoType != saved.identity.ammoType ||
        state->count != saved.state.count || state->max != saved.state.max)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Destination pool facts differ from checkpoint"));
    return Result<void>::success();
}
bool unclaimed(WeaponEntity& target) noexcept {
    const auto* identity   = existing<WeaponEntity::Identity>(target);
    const auto* definition = existing<WeaponEntity::Definition>(target);
    const auto* binding    = existing<WeaponEntity::DefinitionBinding>(target);
    return identity && definition && binding && identity->id.empty() && identity->defId.empty() && !definition->def &&
           !definition->owned && binding->identity.instanceId.isNil() && !binding->identity.definition.id().isValid() &&
           binding->identity.definitionGeneration.isZero();
}
Result<void> required(WeaponEntity& target) {
    if (!existing<WeaponEntity::Identity>(target) || !existing<WeaponEntity::Definition>(target) ||
        !existing<WeaponEntity::DefinitionBinding>(target) || !existing<WeaponEntity::State>(target) ||
        !existing<WeaponEntity::Aim>(target))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Weapon target lacks canonical components"));
    return Result<void>::success();
}
}  // namespace
struct PreparedWeaponRestore::Impl {
    std::weak_ptr<ecs::Table>                              world;
    ecs::EntityHandle                                      target;
    PersistentId                                           originalInstance;
    WeaponEntity::Identity                                 identity;
    WeaponEntity::Definition                               definition;
    WeaponEntity::DefinitionBinding                        binding;
    WeaponEntity::State                                    state;
    WeaponEntity::Aim                                      aim;
    std::optional<CapturedAmmoPool>                        pool;
    std::optional<attributes::PreparedAttributeProjection> attributes;
    std::string                                            attributeSubject;
};
PreparedWeaponRestore::PreparedWeaponRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedWeaponRestore::~PreparedWeaponRestore()                                                  = default;
PreparedWeaponRestore::PreparedWeaponRestore(PreparedWeaponRestore&&) noexcept                   = default;
PreparedWeaponRestore&        PreparedWeaponRestore::operator=(PreparedWeaponRestore&&) noexcept = default;
Result<PreparedWeaponRestore> PreparedWeaponRestore::prepareFacts(const WeaponCapture&                 captured,
                                                                  std::shared_ptr<ecs::Table>          world,
                                                                  WeaponEntity&                        target,
                                                                  std::span<const WeaponCaptureTarget> targets) {
    using R    = Result<PreparedWeaponRestore>;
    auto facts = captured.facts();
    if (!facts) return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Weapon capture moved"));
    const auto handle = ecs::handle_of(&target);
    if (!world || handle.table != world.get() || ecs::try_get(handle) != &target)
        return R::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Weapon restore needs actual owned target world"));
    if (world->is_deferred())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Weapon restore needs idle world"));
    auto valid = required(target);
    if (!valid) return R::failure(valid.status());
    const auto& saved    = facts->get();
    const auto  original = existing<WeaponEntity::DefinitionBinding>(target)->identity.instanceId;
    if (original.isNil() && !unclaimed(target))
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                            "Weapon placeholder already has definition or identity ownership"));
    if (!original.isNil() && original != saved.binding.identity.instanceId)
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Weapon instance differs from checkpoint"));
    auto impl              = std::make_unique<Impl>();
    impl->world            = world;
    impl->target           = handle;
    impl->originalInstance = original;
    impl->identity         = saved.identity;
    impl->binding          = saved.binding;
    impl->aim              = saved.aim;
    impl->definition.owned = std::make_shared<const WeaponDefinition>(saved.definition);
    impl->definition.def   = impl->definition.owned.get();
    auto&       state      = impl->state;
    const auto& value      = saved.state;
    state.resource         = value.resource;
    state.cooldown         = value.cooldown;
    state.burstRemaining   = value.burstRemaining;
    state.burstTimer       = value.burstTimer;
    state.jammed           = value.jammed;
    state.stage            = value.stage;
    state.stageTimer       = value.stageTimer;
    state.currentSpread    = value.currentSpread;
    state.recoilPitch      = value.recoilPitch;
    state.recoilYaw        = value.recoilYaw;
    state.selector         = value.selector;
    state.aiming           = value.aiming;
    state.lastRequest      = saved.request;
    if (saved.stages) {
        state.ownedStages = std::make_shared<const AttackStageSpec>(*saved.stages);
        state.stages      = state.ownedStages.get();
    }
    if (saved.target.isValid()) {
        for (const auto& mapping : targets)
            if (mapping.subject == saved.target) {
                if (mapping.entity.table != world.get() || !ecs::try_get(mapping.entity) ||
                    (state.lastRequest.targetHandle.table &&
                     !sameHandle(state.lastRequest.targetHandle, mapping.entity)))
                    return R::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "Weapon target mapping is stale or ambiguous"));
                state.lastRequest.targetHandle = mapping.entity;
            }
        if (!state.lastRequest.targetHandle.table)
            return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "Weapon target mapping missing"));
    }
    impl->pool = saved.pool;
    if (auto native = captured.attributes()) {
        auto candidate = attributes::PreparedAttributeProjection::prepare(native->get());
        if (!candidate) return R::failure(candidate.status());
        impl->attributeSubject = native->get().values()->get().subject();
        impl->attributes.emplace(std::move(candidate).takeValue());
    }
    return R::success(PreparedWeaponRestore(std::move(impl)));
}
Result<void> PreparedWeaponRestore::validateCore() const {
    if (!impl_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Weapon restore consumed"));
    auto world = impl_->world.lock();
    if (!world || world->is_deferred())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Weapon target world retired or busy"));
    auto* target = dynamic_cast<WeaponEntity*>(ecs::try_get(impl_->target));
    if (!target)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Weapon target incarnation retired"));
    auto valid = required(*target);
    if (!valid) return valid;
    if (impl_->originalInstance.isNil() && !unclaimed(*target))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Weapon placeholder was claimed after preparation"));
    if (existing<WeaponEntity::DefinitionBinding>(*target)->identity.instanceId != impl_->originalInstance)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Weapon target identity changed"));
    if (impl_->state.lastRequest.targetHandle.table && !ecs::try_get(impl_->state.lastRequest.targetHandle))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Weapon staged target retired"));
    return Result<void>::success();
}
Result<void> PreparedWeaponRestore::validatePool() const {
    auto world = impl_->world.lock();
    if (!world) return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Weapon world retired"));
    if (impl_->pool) {
        auto validPool = poolMatches(impl_->state.ammoPool, *impl_->pool, *world);
        if (!validPool) return validPool;
    }
    return Result<void>::success();
}
void PreparedWeaponRestore::bindPreparedPool(AmmoPoolLink link) noexcept { impl_->state.ammoPool = std::move(link); }
void PreparedWeaponRestore::materializeAttributes(std::span<ecs::EntityHandle> journal, std::size_t& count) {
    if (!impl_->attributes) return;
    auto* target   = static_cast<WeaponEntity*>(ecs::try_get(impl_->target));
    auto* selected = existing<WeaponEntity::Attributes>(*target);
    if (!selected) {
        selected         = &*target->attributes();
        journal[count++] = impl_->target;
        if (!impl_->attributeSubject.empty())
            selected->values = attributes::AttributeProjection(impl_->attributeSubject);
    }
}
Result<void> PreparedWeaponRestore::validateAttributes() const {
    if (!impl_->attributes) return Result<void>::success();
    auto* target   = static_cast<WeaponEntity*>(ecs::try_get(impl_->target));
    auto* selected = existing<WeaponEntity::Attributes>(*target);
    if (!selected)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Weapon attributes not materialized"));
    return selected->values.validateRestore(*impl_->attributes, impl_->target);
}
void PreparedWeaponRestore::cancelAttributes(std::span<const ecs::EntityHandle> journal) noexcept {
    for (const auto handle : journal) {
        auto* target = static_cast<WeaponEntity*>(ecs::try_get(handle));
        if (!target) continue;
        auto* buffer = target->getComponentManager().getComponentBuffer<WeaponEntity::Attributes>();
        if (buffer) buffer->reset_slot(target->id);
    }
}
void PreparedWeaponRestore::publish() noexcept {
    auto* target = static_cast<WeaponEntity*>(ecs::try_get(impl_->target));
    if (impl_->attributes)
        existing<WeaponEntity::Attributes>(*target)
            ->values.restore(std::move(*impl_->attributes), impl_->target)
            .expect("prevalidated immutable weapon attribute publication");
    static_assert(std::is_nothrow_swappable_v<WeaponEntity::Identity> &&
                  std::is_nothrow_swappable_v<WeaponEntity::Definition> &&
                  std::is_nothrow_swappable_v<WeaponEntity::DefinitionBinding> &&
                  std::is_nothrow_swappable_v<WeaponEntity::State> && std::is_nothrow_swappable_v<WeaponEntity::Aim>);
    using std::swap;
    swap(*existing<WeaponEntity::Identity>(*target), impl_->identity);
    swap(*existing<WeaponEntity::Definition>(*target), impl_->definition);
    swap(*existing<WeaponEntity::DefinitionBinding>(*target), impl_->binding);
    swap(*existing<WeaponEntity::State>(*target), impl_->state);
    swap(*existing<WeaponEntity::Aim>(*target), impl_->aim);
    if (!impl_->attributes) {
        auto* buffer = target->getComponentManager().getComponentBuffer<WeaponEntity::Attributes>();
        if (buffer) buffer->reset_slot(target->id);
    }
}
void                          PreparedWeaponRestore::consume() noexcept { impl_.reset(); }
Result<PreparedWeaponRestore> PreparedWeaponRestore::prepare(const WeaponCapture&        captured,
                                                             std::shared_ptr<ecs::Table> world, WeaponEntity& target,
                                                             std::span<const WeaponCaptureTarget> targets,
                                                             std::span<const WeaponRestorePool>   pools) {
    auto built = prepareFacts(captured, world, target, targets);
    if (!built) return built;
    auto& plan = *built.value().impl_;
    if (plan.pool) {
        for (const auto& mapping : pools)
            if (mapping.archiveId == plan.pool->archiveId) {
                auto valid = poolMatches(mapping.pool, *plan.pool, *world);
                if (!valid) return Result<PreparedWeaponRestore>::failure(valid.status());
                if (plan.state.ammoPool.isBound() && !sameHandle(plan.state.ammoPool.handle(), mapping.pool.handle()))
                    return Result<PreparedWeaponRestore>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "Weapon pool mapping is ambiguous"));
                plan.state.ammoPool = mapping.pool;
            }
        if (!plan.state.ammoPool.isBound())
            return Result<PreparedWeaponRestore>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Weapon pool archive mapping missing"));
    }
    return built;
}
Result<void> PreparedWeaponRestore::commit() {
    auto world = impl_ ? impl_->world.lock() : nullptr;
    auto valid = validateCore();
    if (!valid) return valid;
    auto poolValid = validatePool();
    if (!poolValid) return poolValid;
    std::array<ecs::EntityHandle, 1> journal;
    std::size_t                      count = 0;
    struct Rollback {
        std::span<ecs::EntityHandle> journal;
        std::size_t&                 count;
        ~Rollback() { PreparedWeaponRestore::cancelAttributes(journal.first(count)); }
    } rollback{journal, count};
    materializeAttributes(journal, count);
    auto attributesValid = validateAttributes();
    if (!attributesValid) return attributesValid;
    publish();
    count = 0;
    consume();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::weapon
