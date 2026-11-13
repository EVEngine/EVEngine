#include "weapon/WeaponGraphRestore.h"
#include <map>
#include <set>
#include <tuple>
namespace eve::weapon {
namespace {
using Key = std::tuple<std::type_index, std::size_t, std::uint64_t>;
Key  key(ecs::EntityHandle handle) { return {handle.type, handle.id, handle.generation}; }
bool samePool(const CapturedAmmoPool& a, const CapturedAmmoPool& b) noexcept {
    return a.archiveId == b.archiveId && a.identity.id == b.identity.id && a.identity.ammoType == b.identity.ammoType &&
           a.state.count == b.state.count && a.state.max == b.state.max;
}
}  // namespace
struct PreparedWeaponGraphRestore::Impl {
    std::weak_ptr<ecs::Table>            world;
    PersistentId                         epoch;
    std::vector<PreparedAmmoPoolRestore> pools;
    std::vector<PreparedWeaponRestore>   weapons;
    std::vector<ecs::EntityHandle>       journal;
    std::vector<AmmoPoolRestoreReceipt>  receipts;
};
PreparedWeaponGraphRestore::PreparedWeaponGraphRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedWeaponGraphRestore::~PreparedWeaponGraphRestore()                                                = default;
PreparedWeaponGraphRestore::PreparedWeaponGraphRestore(PreparedWeaponGraphRestore&&) noexcept            = default;
PreparedWeaponGraphRestore& PreparedWeaponGraphRestore::operator=(PreparedWeaponGraphRestore&&) noexcept = default;
Result<PreparedWeaponGraphRestore> PreparedWeaponGraphRestore::prepare(PersistentId                         epoch,
                                                                       std::shared_ptr<ecs::Table>          world,
                                                                       std::span<const WeaponGraphMember>   weapons,
                                                                       std::span<const WeaponGraphPool>     pools,
                                                                       std::span<const WeaponCaptureTarget> targets) {
    using R = Result<PreparedWeaponGraphRestore>;
    if (epoch.isNil() || !world)
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Graph needs epoch and owned Table"));
    if (world->is_deferred())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Graph needs idle Table"));
    auto impl   = std::make_unique<Impl>();
    impl->world = world;
    impl->epoch = epoch;
    impl->pools.reserve(pools.size());
    impl->weapons.reserve(weapons.size());
    impl->journal.resize(weapons.size());
    impl->receipts.resize(pools.size());
    std::map<std::string, std::size_t> byArchive;
    std::set<Key>                      poolTargets, weaponTargets;
    std::set<std::string>              instances;
    for (const auto& pool : pools) {
        if (pool.target.table != world.get() || !poolTargets.insert(key(pool.target)).second ||
            !byArchive.emplace(pool.captured.archiveId.format(), impl->pools.size()).second)
            return R::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Pool graph has foreign or duplicate correspondence"));
        auto* entity = dynamic_cast<AmmoPoolEntity*>(ecs::try_get(pool.target));
        if (!entity) return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Graph pool target retired"));
        auto plan = PreparedAmmoPoolRestore::prepare(pool.captured, world, *entity);
        if (!plan) return R::failure(plan.status());
        impl->pools.push_back(std::move(plan).takeValue());
    }
    for (const auto& member : weapons) {
        auto facts = member.captured.get().facts();
        if (!facts || member.captured.get().saveEpoch() != epoch)
            return R::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Graph source epoch differs or capture moved"));
        if (member.target.table != world.get() || !weaponTargets.insert(key(member.target)).second ||
            !instances.insert(facts->get().binding.identity.instanceId.format()).second)
            return R::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Graph weapon correspondence is foreign or duplicated"));
        auto* entity = dynamic_cast<WeaponEntity*>(ecs::try_get(member.target));
        if (!entity) return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Graph weapon target retired"));
        auto plan = PreparedWeaponRestore::prepareFacts(member.captured.get(), world, *entity, targets);
        if (!plan) return R::failure(plan.status());
        if (const auto& pool = facts->get().pool) {
            auto found = byArchive.find(pool->archiveId.format());
            if (found == byArchive.end())
                return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "Graph pool mapping missing"));
            auto& owner = impl->pools[found->second];
            if (!samePool(*pool, owner.captured()))
                return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Shared pool checkpoint facts disagree"));
            plan.value().bindPreparedPool(owner.futureLink());
        }
        impl->weapons.push_back(std::move(plan).takeValue());
    }
    return R::success(PreparedWeaponGraphRestore(std::move(impl)));
}
Result<WeaponGraphRestoreReceipt> PreparedWeaponGraphRestore::commit() {
    using R = Result<WeaponGraphRestoreReceipt>;
    if (!impl_) return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Graph candidate consumed"));
    auto world = impl_->world.lock();
    if (!world || world->is_deferred())
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Graph target world retired or busy"));
    for (const auto& pool : impl_->pools) {
        auto valid = pool.validate();
        if (!valid) return R::failure(valid.status());
    }
    for (const auto& weapon : impl_->weapons) {
        auto valid = weapon.validateCore();
        if (!valid) return R::failure(valid.status());
    }
    std::size_t count = 0;
    struct Rollback {
        std::span<ecs::EntityHandle> journal;
        std::size_t&                 count;
        void (*cancel)(std::span<const ecs::EntityHandle>) noexcept;
        ~Rollback() {
            if (count) cancel(journal.first(count));
        }
    } rollback{impl_->journal, count, &PreparedWeaponRestore::cancelAttributes};
    for (auto& weapon : impl_->weapons) weapon.materializeAttributes(impl_->journal, count);
    for (const auto& weapon : impl_->weapons) {
        auto valid = weapon.validateAttributes();
        if (!valid) return R::failure(valid.status());
    }
    for (std::size_t i = 0; i < impl_->pools.size(); ++i) impl_->receipts[i] = impl_->pools[i].publish();
    for (auto& weapon : impl_->weapons) weapon.publish();
    count                           = 0;
    const bool                empty = impl_->pools.empty() && impl_->weapons.empty();
    WeaponGraphRestoreReceipt receipt{impl_->epoch, impl_->weapons.size(), std::move(impl_->receipts)};
    // Invalidate the public token before retiring any old weapon payload; every target is already published.
    auto retired = std::move(impl_);
    retired.reset();
    return R::success(std::move(receipt), Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}
}  // namespace eve::weapon
