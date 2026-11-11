#include "rts/RTSWeaponRestore.h"
#include <map>
#include <set>
#include <type_traits>
#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"
#include "rts/RTSWeaponCapture.h"
#include "weapon/WeaponGraphRestore.h"
namespace eve::rts {
namespace {
bool same(ecs::EntityHandle a, ecs::EntityHandle b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
template <class T>
Result<T> reject(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message));
}
ResultRef<WeaponLink> bodyLink(ecs::EntityHandle handle, SubjectRef subject, RTSWeaponBodyKind kind) {
    auto* entity = ecs::try_get(handle);
    if (!entity || snapshot_internal::subjectOf(entity) != subject)
        return reject<std::reference_wrapper<WeaponLink>>(DiagnosticCode::StaleHandle,
                                                          "Weapon body incarnation changed");
    if (kind == RTSWeaponBodyKind::Unit) {
        auto* unit      = dynamic_cast<Unit*>(entity);
        auto* component = unit ? detail::existingComponent<Unit::Weapon>(*unit) : nullptr;
        if (component) return ResultRef<WeaponLink>::success(std::ref(component->link));
    } else if (kind == RTSWeaponBodyKind::Building) {
        auto* building  = dynamic_cast<Building*>(entity);
        auto* component = building ? detail::existingComponent<Building::Weapon>(*building) : nullptr;
        if (component) return ResultRef<WeaponLink>::success(std::ref(component->link));
    }
    return reject<std::reference_wrapper<WeaponLink>>(DiagnosticCode::Conflict,
                                                      "Weapon body kind or required component differs");
}
void releaseRows(const std::weak_ptr<ecs::Table>& weak, std::span<const ecs::EntityHandle> rows) noexcept {
    if (auto world = weak.lock()) {
        ecs::ScopedTable scope(*world);
        for (auto handle : rows)
            if (auto* entity = ecs::try_get(handle)) entity->release();
    }
}
}  // namespace
struct RestoredRTSWeaponPools::Impl {
    std::weak_ptr<ecs::Table>                   world;
    PersistentId                                epoch;
    std::vector<ecs::EntityHandle>              rows;
    std::vector<weapon::AmmoPoolRestoreReceipt> receipts;
    ~Impl() { releaseRows(world, rows); }
};
RestoredRTSWeaponPools::RestoredRTSWeaponPools(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RestoredRTSWeaponPools::~RestoredRTSWeaponPools()                                            = default;
RestoredRTSWeaponPools::RestoredRTSWeaponPools(RestoredRTSWeaponPools&&) noexcept            = default;
RestoredRTSWeaponPools& RestoredRTSWeaponPools::operator=(RestoredRTSWeaponPools&&) noexcept = default;
PersistentId RestoredRTSWeaponPools::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::span<const weapon::AmmoPoolRestoreReceipt> RestoredRTSWeaponPools::pools() const noexcept {
    return impl_ ? std::span<const weapon::AmmoPoolRestoreReceipt>(impl_->receipts)
                 : std::span<const weapon::AmmoPoolRestoreReceipt>{};
}
Result<void> RestoredRTSWeaponPools::close() {
    if (!impl_) return Result<void>::success(Status::success(StatusCode::NoOp));
    auto world = impl_->world.lock();
    if (world && world->is_deferred())
        return reject<void>(DiagnosticCode::PreconditionViolation, "Pool owner close needs idle destination Table");
    const bool changed = world && !impl_->rows.empty();
    impl_.reset();
    return Result<void>::success(Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}
struct PreparedRTSWeaponRestore::Impl {
    /** @ownership Observed physical facade guarded by lifetime and locked actual Table.
     * @lifetime Never dereferenced during CPU candidate destruction; only this live facade may publish.
     */
    RTS*                           target;
    LifetimeWitness                lifetime;
    std::weak_ptr<ecs::Table>      world;
    PersistentId                   epoch;
    std::vector<ecs::EntityHandle> units, buildings;
    struct Body {
        SubjectRef                 subject;
        ecs::EntityHandle          target;
        RTSWeaponBodyKind          kind;
        std::optional<std::size_t> gun;
    };
    std::vector<Body>                                         bodies;
    std::vector<std::shared_ptr<const weapon::WeaponCapture>> guns;
    std::vector<weapon::CapturedAmmoPool>                     pools;
    std::vector<weapon::WeaponCaptureTarget>                  targets;
    Result<void>                                              validate(RTS& domain) const {
        if (target != &domain || !lifetime.isAlive())
            return reject<void>(DiagnosticCode::StaleHandle, "Weapon restore facade retired or foreign");
        auto table = world.lock();
        if (!table || table != domain.ownedWorld_ || table->is_deferred())
            return reject<void>(DiagnosticCode::PreconditionViolation, "Weapon target world retired or busy");
        if (!domain.weapons_.empty())
            return reject<void>(DiagnosticCode::Conflict, "Weapon ownership scope already claimed");
        if (domain.units_.size() != units.size() || domain.buildings_.size() != buildings.size())
            return reject<void>(DiagnosticCode::Conflict, "Weapon body topology changed");
        for (std::size_t i = 0; i < units.size(); ++i)
            if (!same(units[i], domain.units_[i]))
                return reject<void>(DiagnosticCode::StaleHandle, "Unit incarnation changed");
        for (std::size_t i = 0; i < buildings.size(); ++i)
            if (!same(buildings[i], domain.buildings_[i]))
                return reject<void>(DiagnosticCode::StaleHandle, "Building incarnation changed");
        for (const auto& body : bodies) {
            auto linked = bodyLink(body.target, body.subject, body.kind);
            if (!linked) return Result<void>::failure(linked.status());
            if (linked.value().get().isBound())
                return reject<void>(DiagnosticCode::Conflict, "Weapon body already claimed");
        }
        for (const auto& target : targets) {
            auto current = domain.resolveOwnedSubject(target.subject);
            if (!current) return Result<void>::failure(current.status());
            if (!same(current.value(), target.entity))
                return reject<void>(DiagnosticCode::StaleHandle, "Weapon request target incarnation changed");
        }
        return Result<void>::success();
    }
};
PreparedRTSWeaponRestore::PreparedRTSWeaponRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSWeaponRestore::~PreparedRTSWeaponRestore()                                                     = default;
PreparedRTSWeaponRestore::PreparedRTSWeaponRestore(PreparedRTSWeaponRestore&&) noexcept                   = default;
PreparedRTSWeaponRestore&        PreparedRTSWeaponRestore::operator=(PreparedRTSWeaponRestore&&) noexcept = default;
Result<PreparedRTSWeaponRestore> PreparedRTSWeaponRestore::prepare(RTS& target, const RTSWeaponCapture& captured) {
    using R = Result<PreparedRTSWeaponRestore>;
    if (captured.saveEpoch().isNil())
        return reject<PreparedRTSWeaponRestore>(DiagnosticCode::InvalidArgument, "Weapon capture moved");
    auto world = target.ownedWorld_;
    if (!world)
        return reject<PreparedRTSWeaponRestore>(DiagnosticCode::Unsupported,
                                                "Weapon restore needs actual owning Table");
    if (world->is_deferred())
        return reject<PreparedRTSWeaponRestore>(DiagnosticCode::PreconditionViolation,
                                                "Weapon restore needs idle Table");
    if (target.units_.size() + target.buildings_.size() != captured.members().size())
        return reject<PreparedRTSWeaponRestore>(DiagnosticCode::Conflict, "Complete weapon body scope differs");
    ecs::ScopedTable scope(*world);
    auto             plan = std::make_unique<Impl>();
    plan->target          = &target;
    plan->lifetime        = target.lifetimeWitness();
    plan->world           = world;
    plan->epoch           = captured.saveEpoch();
    plan->units           = target.units_;
    plan->buildings       = target.buildings_;
    std::map<PersistentId, std::pair<ecs::EntityHandle, RTSWeaponBodyKind>> bodies;
    for (const auto* handles : {&target.units_, &target.buildings_}) {
        const auto kind = handles == &target.units_ ? RTSWeaponBodyKind::Unit : RTSWeaponBodyKind::Building;
        for (auto handle : *handles) {
            auto subject = snapshot_internal::subjectOf(handle);
            if (handle.table != world.get() || !subject.isValid() ||
                !bodies.emplace(subject.persistentId(), std::pair{handle, kind}).second)
                return reject<PreparedRTSWeaponRestore>(DiagnosticCode::Conflict,
                                                        "Invalid native weapon body identity");
        }
    }
    plan->bodies.reserve(captured.members().size());
    plan->guns.reserve(captured.members().size());
    std::map<PersistentId, std::size_t>              guns;
    std::map<PersistentId, weapon::CapturedAmmoPool> pools;
    std::set<PersistentId>                           targets;
    for (const auto& member : captured.members()) {
        auto found = bodies.find(member.body.persistentId());
        if (found == bodies.end() || found->second.second != member.kind)
            return reject<PreparedRTSWeaponRestore>(DiagnosticCode::Conflict, "Weapon body identity or kind differs");
        std::optional<std::size_t> gun;
        if (member.weapon) {
            auto facts = member.weapon->facts();
            if (!facts || member.weapon->saveEpoch() != plan->epoch || facts->get().binding.identity.instanceId.isNil())
                return reject<PreparedRTSWeaponRestore>(DiagnosticCode::Conflict,
                                                        "Weapon facts or checkpoint scope invalid");
            auto [entry, inserted] = guns.emplace(facts->get().binding.identity.instanceId, plan->guns.size());
            if (inserted) {
                plan->guns.push_back(member.weapon);
                if (const auto& pool = facts->get().pool) pools.emplace(pool->archiveId, *pool);
                if (const auto subject = facts->get().target;
                    subject.isValid() && targets.insert(subject.persistentId()).second) {
                    auto resolved = target.resolveOwnedSubject(subject);
                    if (!resolved) return R::failure(resolved.status());
                    plan->targets.push_back({std::move(resolved).takeValue(), subject});
                }
            } else if (plan->guns[entry->second].get() != member.weapon.get())
                return reject<PreparedRTSWeaponRestore>(DiagnosticCode::Conflict,
                                                        "Distinct captured weapons share instance identity");
            gun = entry->second;
        }
        plan->bodies.push_back({member.body, found->second.first, member.kind, gun});
    }
    plan->pools.reserve(pools.size());
    for (auto& [id, facts] : pools) plan->pools.push_back(std::move(facts));
    auto valid = plan->validate(target);
    if (!valid) return R::failure(valid.status());
    return R::success(PreparedRTSWeaponRestore(std::move(plan)));
}
Result<RestoredRTSWeaponPools> RTS::restoreWeapons(PreparedRTSWeaponRestore&& prepared) {
    using R = Result<RestoredRTSWeaponPools>;
    if (!prepared.impl_)
        return reject<RestoredRTSWeaponPools>(DiagnosticCode::PreconditionViolation, "Weapon graph candidate consumed");
    auto* plan  = prepared.impl_.get();
    auto  valid = plan->validate(*this);
    if (!valid) return R::failure(valid.status());
    auto             world = plan->world.lock();
    ecs::ScopedTable scope(*world);
    auto             owner = std::make_unique<RestoredRTSWeaponPools::Impl>();
    owner->world           = world;
    owner->epoch           = plan->epoch;
    std::vector<ecs::EntityHandle> guns, pools;
    guns.reserve(plan->guns.size());
    pools.reserve(plan->pools.size());
    struct Rollback {
        std::weak_ptr<ecs::Table>       world;
        std::vector<ecs::EntityHandle>& guns;
        std::vector<ecs::EntityHandle>& pools;
        bool                            published = false;
        ~Rollback() {
            if (!published) {
                releaseRows(world, guns);
                releaseRows(world, pools);
            }
        }
    } rollback{world, guns, pools};
    std::vector<weapon::WeaponGraphMember> members;
    std::vector<weapon::WeaponGraphPool>   poolMappings;
    std::vector<WeaponLink>                bodyLinks(plan->bodies.size());
    members.reserve(plan->guns.size());
    poolMappings.reserve(plan->pools.size());
    for (const auto& facts : plan->guns) {
        auto* gun = weapon::WeaponEntity::createWeapon();
        guns.push_back(ecs::handle_of(gun));
        members.push_back({std::cref(*facts), guns.back()});
    }
    for (const auto& facts : plan->pools) {
        auto* pool = weapon::AmmoPoolEntity::createPool();
        pools.push_back(ecs::handle_of(pool));
        poolMappings.push_back({facts, pools.back()});
    }
    for (std::size_t i = 0; i < plan->bodies.size(); ++i)
        if (const auto gun = plan->bodies[i].gun) {
            auto linked = WeaponLink::bind(guns[*gun]);
            if (!linked) return R::failure(linked.status());
            bodyLinks[i] = std::move(linked).takeValue();
        }
    auto graph = weapon::PreparedWeaponGraphRestore::prepare(plan->epoch, world, members, poolMappings, plan->targets);
    if (!graph) return R::failure(graph.status());
    auto published = graph.value().commit();
    if (!published) return R::failure(published.status());
    auto receipt = std::move(published).takeValue();
    static_assert(std::is_nothrow_move_assignable_v<WeaponLink>);
    for (std::size_t i = 0; i < plan->bodies.size(); ++i) {
        const auto& body = plan->bodies[i];
        bodyLink(body.target, body.subject, body.kind).expect("prevalidated body weapon publication").get() =
            std::move(bodyLinks[i]);
    }
    weapons_.swap(guns);
    owner->rows.swap(pools);
    owner->receipts    = std::move(receipt.pools);
    rollback.published = true;
    const bool empty   = plan->guns.empty();
    auto       retired = std::move(prepared.impl_);
    retired.reset();
    return R::success(RestoredRTSWeaponPools(std::move(owner)),
                      Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}
}  // namespace eve::rts
