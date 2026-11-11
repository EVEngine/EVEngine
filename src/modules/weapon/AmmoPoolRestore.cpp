#include "weapon/AmmoPoolRestore.h"
#include <type_traits>
namespace eve::weapon {
namespace {
template <class T>
T* existing(AmmoPoolEntity& entity) noexcept {
    auto* buffer = entity.getComponentManager().template getComponentBuffer<T>();
    return buffer && buffer->has(entity.id) ? buffer->ptr(entity.id) : nullptr;
}
Result<void> required(AmmoPoolEntity& target) {
    if (!existing<AmmoPoolEntity::Identity>(target) || !existing<AmmoPoolEntity::State>(target) ||
        !existing<AmmoPoolEntity::Lifetime>(target))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Pool target lacks canonical components"));
    return Result<void>::success();
}
}  // namespace
struct PreparedAmmoPoolRestore::Impl {
    std::weak_ptr<ecs::Table>      world;
    ecs::EntityHandle              target;
    CapturedAmmoPool               facts;
    bool                           originallyBound = false;
    LifetimeWitness                originalLifetime;
    std::unique_ptr<LifetimeOwner> replacement;
};
PreparedAmmoPoolRestore::PreparedAmmoPoolRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedAmmoPoolRestore::~PreparedAmmoPoolRestore()                                                    = default;
PreparedAmmoPoolRestore::PreparedAmmoPoolRestore(PreparedAmmoPoolRestore&&) noexcept                   = default;
PreparedAmmoPoolRestore&        PreparedAmmoPoolRestore::operator=(PreparedAmmoPoolRestore&&) noexcept = default;
Result<PreparedAmmoPoolRestore> PreparedAmmoPoolRestore::prepare(const CapturedAmmoPool&     captured,
                                                                 std::shared_ptr<ecs::Table> world,
                                                                 AmmoPoolEntity&             target) {
    using R = Result<PreparedAmmoPoolRestore>;
    if (captured.archiveId.isNil())
        return R::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil pool archive reference required"));
    const auto handle = ecs::handle_of(&target);
    if (!world || handle.table != world.get() || ecs::try_get(handle) != &target)
        return R::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Pool restore needs actual owned destination"));
    if (world->is_deferred())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Pool restore needs idle world"));
    auto valid = required(target);
    if (!valid) return R::failure(valid.status());
    auto impl             = std::make_unique<Impl>();
    impl->world           = world;
    impl->target          = handle;
    impl->facts           = captured;
    auto& lifetime        = *existing<AmmoPoolEntity::Lifetime>(target);
    impl->originallyBound = static_cast<bool>(lifetime.owner_);
    if (lifetime.owner_) impl->originalLifetime = lifetime.owner_->witness();
    impl->replacement = std::make_unique<LifetimeOwner>();
    return R::success(PreparedAmmoPoolRestore(std::move(impl)));
}
Result<void> PreparedAmmoPoolRestore::validate() const {
    using R = Result<void>;
    if (!impl_) return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Pool restore consumed"));
    auto world = impl_->world.lock();
    if (!world || world->is_deferred())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Pool destination retired or busy"));
    auto* target = dynamic_cast<AmmoPoolEntity*>(ecs::try_get(impl_->target));
    if (!target) return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Pool incarnation retired"));
    auto valid = required(*target);
    if (!valid) return R::failure(valid.status());
    auto& lifetime = *existing<AmmoPoolEntity::Lifetime>(*target);
    if (impl_->originallyBound != static_cast<bool>(lifetime.owner_) ||
        (impl_->originallyBound && !impl_->originalLifetime.isAlive()))
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Pool lifetime changed after preparation"));
    return R::success();
}
const CapturedAmmoPool& PreparedAmmoPoolRestore::captured() const noexcept { return impl_->facts; }
AmmoPoolLink            PreparedAmmoPoolRestore::futureLink() const noexcept {
    AmmoPoolLink link;
    link.handle_   = impl_->target;
    link.lifetime_ = impl_->replacement->witness();
    return link;
}
AmmoPoolRestoreReceipt PreparedAmmoPoolRestore::publish() noexcept {
    auto* target   = static_cast<AmmoPoolEntity*>(ecs::try_get(impl_->target));
    auto& lifetime = *existing<AmmoPoolEntity::Lifetime>(*target);
    auto  link     = futureLink();
    static_assert(std::is_nothrow_swappable_v<AmmoPoolEntity::Identity> &&
                  std::is_nothrow_swappable_v<AmmoPoolEntity::State>);
    using std::swap;
    swap(*existing<AmmoPoolEntity::Identity>(*target), impl_->facts.identity);
    swap(*existing<AmmoPoolEntity::State>(*target), impl_->facts.state);
    swap(lifetime.owner_, impl_->replacement);
    const auto archive = impl_->facts.archiveId;
    impl_.reset();
    return {archive, std::move(link)};
}
Result<AmmoPoolRestoreReceipt> PreparedAmmoPoolRestore::commit() {
    auto world   = impl_ ? impl_->world.lock() : nullptr;
    auto checked = validate();
    if (!checked) return Result<AmmoPoolRestoreReceipt>::failure(checked.status());
    return Result<AmmoPoolRestoreReceipt>::success(publish(), Status::success(StatusCode::Applied));
}
}  // namespace eve::weapon
