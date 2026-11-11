/** @file AmmoPoolLink.cpp
 * @brief weapon module-interface: typed pool links; no capability/event/script/tool/save protocol additions.
 * ECS: Weapon State owns the link, AmmoPool Lifetime owns its witness. Bind is explicit outside Views;
 * resolution reads existing components only. Pool component publication invalidates links until explicit rebind.
 */
#include "weapon/WeaponTypes.h"
namespace eve::weapon {
namespace {
template <class T, class Entity>
T* existing(Entity& entity) noexcept {
    auto* buffer = entity.getComponentManager().template getComponentBuffer<T>();
    return buffer && buffer->has(entity.id) ? buffer->ptr(entity.id) : nullptr;
}
}  // namespace
AmmoPoolEntity::Lifetime::Lifetime()  = default;
AmmoPoolEntity::Lifetime::~Lifetime() = default;
AmmoPoolEntity::Lifetime::Lifetime(const Lifetime&) {}
AmmoPoolEntity::Lifetime& AmmoPoolEntity::Lifetime::operator=(const Lifetime&) {
    owner_.reset();
    return *this;
}
AmmoPoolEntity::Lifetime::Lifetime(Lifetime&& other) noexcept { other.owner_.reset(); }
AmmoPoolEntity::Lifetime& AmmoPoolEntity::Lifetime::operator=(Lifetime&& other) noexcept {
    owner_.reset();
    other.owner_.reset();
    return *this;
}
Result<AmmoPoolLink> AmmoPoolLink::bind(WeaponEntity& weapon, AmmoPoolEntity& pool) {
    const auto source = ecs::handle_of(&weapon), target = ecs::handle_of(&pool);
    if (!source.table || source.table != target.table || ecs::try_get(source) != &weapon ||
        ecs::try_get(target) != &pool)
        return Result<AmmoPoolLink>::failure(Diagnostic::error(
            DiagnosticCode::StaleHandle, "Ammunition links require live endpoints in the same owning world"));
    if (source.table->is_deferred())
        return Result<AmmoPoolLink>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Ammunition binding requires an idle world outside Views"));
    auto* lifetime = existing<AmmoPoolEntity::Lifetime>(pool);
    if (!lifetime || !existing<AmmoPoolEntity::Identity>(pool) || !existing<AmmoPoolEntity::State>(pool) ||
        !existing<WeaponEntity::State>(weapon))
        return Result<AmmoPoolLink>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Ammunition endpoint components are missing"));
    if (!lifetime->owner_) lifetime->owner_ = std::make_unique<LifetimeOwner>();
    AmmoPoolLink result;
    result.handle_   = target;
    result.lifetime_ = lifetime->owner_->witness();
    return Result<AmmoPoolLink>::success(std::move(result));
}
AmmoPoolEntity* AmmoPoolLink::resolve() const noexcept {
    if (!lifetime_.isAlive()) return nullptr;
    auto* pool = dynamic_cast<AmmoPoolEntity*>(ecs::try_get(handle_));
    if (!pool || !existing<AmmoPoolEntity::Identity>(*pool) || !existing<AmmoPoolEntity::State>(*pool)) return nullptr;
    return pool;
}
bool AmmoPoolLink::isStale() const noexcept { return isBound() && !resolve(); }
}  // namespace eve::weapon
