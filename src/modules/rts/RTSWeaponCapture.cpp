#include "rts/RTSWeaponCapture.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"
#include "weapon/WeaponCapture.h"
namespace eve::rts {
static_assert(int(RTSWeaponBodyKind::Unit) == 0 && int(RTSWeaponBodyKind::Building) == 1);
namespace {
template <class T>
Result<T> invalidGraph(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::Conflict, message, "rts.body-weapons"));
}
Result<PersistentId> graphId(const Value& value) {
    const auto* text = value.getIf<std::string>();
    const auto  id   = text ? PersistentId::parse(*text) : std::nullopt;
    if (!id || id->isNil() || id->format() != *text)
        return invalidGraph<PersistentId>("Expected canonical non-nil graph UUID");
    return Result<PersistentId>::success(*id);
}
Result<Value> encodeGraph(std::span<const RTSWeaponMember> members, PersistentId epoch) {
    using R = Result<Value>;
    if (epoch.isNil()) return invalidGraph<Value>("Non-nil weapon graph epoch required");
    Value::Array bodies;
    bodies.reserve(members.size());
    struct Node {
        std::shared_ptr<const weapon::WeaponCapture> owner;
        Value                                        value;
    };
    std::map<PersistentId, Node>  weapons;
    std::map<PersistentId, Value> pools;
    PersistentId                  previous;
    for (const auto& member : members) {
        const auto subject = member.body.persistentId();
        if (subject.isNil() || (!previous.isNil() && !(previous < subject)) ||
            (member.kind != RTSWeaponBodyKind::Unit && member.kind != RTSWeaponBodyKind::Building))
            return invalidGraph<Value>("Weapon body identity order or kind invalid");
        previous = subject;
        Value reference;
        if (member.weapon) {
            const auto facts = member.weapon->facts();
            if (!facts || member.weapon->saveEpoch() != epoch)
                return invalidGraph<Value>("Weapon graph contains moved or foreign-epoch facts");
            const auto instance = facts->get().binding.identity.instanceId;
            if (instance.isNil()) return invalidGraph<Value>("Weapon graph contains nil weapon identity");
            auto found = weapons.find(instance);
            if (found == weapons.end()) {
                auto encoded = member.weapon->encodeState();
                if (!encoded) return R::failure(encoded.status());
                auto value = std::move(encoded).takeValue();
                if (const auto& pool = facts->get().pool) {
                    const auto& poolValue  = value.getIf<Value::Object>()->at("pool");
                    auto [entry, inserted] = pools.emplace(pool->archiveId, poolValue);
                    if (!inserted && entry->second != poolValue)
                        return invalidGraph<Value>("Weapons disagree about shared pool facts");
                }
                weapons.emplace(instance, Node{member.weapon, std::move(value)});
            } else if (found->second.owner.get() != member.weapon.get())
                return invalidGraph<Value>("Distinct weapon owners share one instance identity");
            reference = Value(instance.format());
        }
        bodies.push_back(Value::Array{member.body.format(), int(member.kind), std::move(reference)});
    }
    Value::Array nodes;
    nodes.reserve(weapons.size());
    for (auto& [id, node] : weapons) nodes.push_back(std::move(node.value));
    return R::success(Value::object({{"members", Value(std::move(bodies))}, {"weapons", Value(std::move(nodes))}}));
}
}  // namespace
struct RTSWeaponCapture::Impl {
    PersistentId                 epoch;
    std::vector<RTSWeaponMember> members;
};
RTSWeaponCapture::RTSWeaponCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSWeaponCapture::~RTSWeaponCapture()                                      = default;
RTSWeaponCapture::RTSWeaponCapture(RTSWeaponCapture&&) noexcept            = default;
RTSWeaponCapture& RTSWeaponCapture::operator=(RTSWeaponCapture&&) noexcept = default;
PersistentId      RTSWeaponCapture::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::span<const RTSWeaponMember> RTSWeaponCapture::members() const noexcept {
    return impl_ ? std::span<const RTSWeaponMember>(impl_->members) : std::span<const RTSWeaponMember>{};
}
Result<SnapshotEnvelope> RTSWeaponCapture::seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const {
    if (!impl_ || revision.isZero()) return invalidGraph<SnapshotEnvelope>("Weapon graph moved or zero save revision");
    auto encoded = encodeGraph(impl_->members, impl_->epoch);
    if (!encoded) return Result<SnapshotEnvelope>::failure(encoded.status());
    return makeSnapshotEnvelope("eve.rts-body-weapons", *LogicalId::fromParts("rts", "body-weapons"), SchemaVersion(1),
                                impl_->epoch, revision, tick, std::move(encoded).takeValue(), hash);
}
Result<RTSWeaponCapture> RTSWeaponCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                 const SnapshotHashProvider& hash) {
    using R       = Result<RTSWeaponCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown body weapon graph version"));
    if (snapshot.type != "eve.rts-body-weapons" || snapshot.schema != *LogicalId::fromParts("rts", "body-weapons") ||
        epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return invalidGraph<RTSWeaponCapture>("Body weapon checkpoint boundary mismatch");
    const auto* root = snapshot.payload.getIf<Value::Object>();
    if (!root || root->size() != 2 || !root->contains("members") || !root->contains("weapons") ||
        !root->at("members").isArray() || !root->at("weapons").isArray())
        return invalidGraph<RTSWeaponCapture>("Unknown or missing body weapon fields");
    std::map<PersistentId, std::shared_ptr<const weapon::WeaponCapture>> nodes;
    PersistentId                                                         previous;
    for (const auto& value : *root->at("weapons").getIf<Value::Array>()) {
        auto parsed = weapon::WeaponCapture::prepareState(value);
        if (!parsed) return R::failure(parsed.status());
        if (parsed.value().saveEpoch() != epoch)
            return invalidGraph<RTSWeaponCapture>("Foreign weapon checkpoint epoch");
        const auto instance = parsed.value().facts()->get().binding.identity.instanceId;
        if (!previous.isNil() && !(previous < instance))
            return invalidGraph<RTSWeaponCapture>("Weapon instances must be unique and ordered");
        previous = instance;
        nodes.emplace(instance, std::make_shared<const weapon::WeaponCapture>(std::move(parsed).takeValue()));
    }
    auto result   = std::make_unique<Impl>();
    result->epoch = epoch;
    std::set<PersistentId> used;
    for (const auto& row : *root->at("members").getIf<Value::Array>()) {
        const auto* tuple = row.getIf<Value::Array>();
        if (!tuple || tuple->size() != 3 || !(*tuple)[1].isInt64() || (*tuple)[1].asInt() < 0 ||
            (*tuple)[1].asInt() > 1)
            return invalidGraph<RTSWeaponCapture>("Invalid body weapon tuple or kind");
        auto body = graphId((*tuple)[0]);
        if (!body) return R::failure(body.status());
        std::shared_ptr<const weapon::WeaponCapture> node;
        if (!(*tuple)[2].isNull()) {
            auto reference = graphId((*tuple)[2]);
            if (!reference) return R::failure(reference.status());
            auto found = nodes.find(reference.value());
            if (found == nodes.end()) return invalidGraph<RTSWeaponCapture>("Body references missing weapon facts");
            used.insert(reference.value());
            node = found->second;
        }
        result->members.push_back({SubjectRef::fromPersistentId(std::move(body).takeValue()), std::move(node),
                                   static_cast<RTSWeaponBodyKind>((*tuple)[1].asInt())});
    }
    if (used.size() != nodes.size()) return invalidGraph<RTSWeaponCapture>("Unreferenced weapon facts in body graph");
    auto valid = encodeGraph(result->members, epoch);
    if (!valid) return R::failure(valid.status());
    return R::success(RTSWeaponCapture(std::move(result)));
}
Result<RTSWeaponCapture> RTS::captureWeapons(PersistentId epoch) const {
    using R     = Result<RTSWeaponCapture>;
    auto reject = [](DiagnosticCode code, const char* message) { return R::failure(Diagnostic::error(code, message)); };
    if (epoch.isNil()) return reject(DiagnosticCode::InvalidArgument, "Weapon closure needs checkpoint epoch");
    if (!ownedWorld_) return reject(DiagnosticCode::Unsupported, "Weapon closure needs actual owned Table");
    if (ownedWorld_->is_deferred())
        return reject(DiagnosticCode::PreconditionViolation, "Weapon closure needs idle Table");
    ecs::ScopedTable scope(*ownedWorld_);
    auto             pools = weapon::WeaponPoolBindings::capture(*ownedWorld_, epoch);
    if (!pools) return R::failure(pools.status());
    auto result   = std::make_unique<RTSWeaponCapture::Impl>();
    result->epoch = epoch;
    result->members.reserve(units_.size() + buildings_.size());
    std::vector<weapon::WeaponCaptureTarget> targets;
    std::set<PersistentId>                   subjects;
    auto                                     addTargets = [&](const auto& handles) {
        for (auto handle : handles) {
            if (handle.table != ownedWorld_.get() || !ecs::try_get(handle)) return false;
            auto subject = snapshot_internal::subjectOf(handle);
            if (!subject.isValid() || !subjects.insert(subject.persistentId()).second) return false;
            targets.push_back({handle, subject});
        }
        return true;
    };
    if (!addTargets(units_) || !addTargets(buildings_) || !addTargets(resourceNodes_) || !addTargets(players_) ||
        !addTargets(factions_) || !addTargets(matches_))
        return reject(DiagnosticCode::Conflict, "Weapon closure roots have stale, foreign or duplicate identities");
    using Key = std::tuple<std::type_index, std::size_t, std::uint64_t>;
    std::map<Key, std::shared_ptr<const weapon::WeaponCapture>> captures;
    std::set<PersistentId>                                      instances;
    auto                                                        captureBody = [&](auto& body) -> Result<void> {
        const auto* linked = detail::existingComponent<typename std::remove_reference_t<decltype(body)>::Weapon>(body);
        if (!linked)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Body weapon component missing"));
        std::shared_ptr<const weapon::WeaponCapture> capture;
        if (linked->link.isBound()) {
            auto handle = linked->link.handle();
            if (handle.table != ownedWorld_.get())
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Foreign body weapon"));
            auto* gun = dynamic_cast<weapon::WeaponEntity*>(linked->link.resolve());
            if (!gun)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Body weapon retired"));
            const Key key{handle.type, handle.id, handle.generation};
            auto      found = captures.find(key);
            if (found == captures.end()) {
                auto frozen = weapon::WeaponCapture::capture(*gun, epoch, targets, pools.value());
                if (!frozen) return Result<void>::failure(frozen.status());
                if (!instances.insert(frozen.value().facts()->get().binding.identity.instanceId).second)
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "Distinct weapons share instance identity"));
                found =
                    captures.emplace(key, std::make_shared<const weapon::WeaponCapture>(std::move(frozen).takeValue()))
                        .first;
            }
            capture = found->second;
        }
        constexpr auto kind = std::is_same_v<std::remove_reference_t<decltype(body)>, Unit>
                                                                                         ? RTSWeaponBodyKind::Unit
                                                                                         : RTSWeaponBodyKind::Building;
        result->members.push_back({snapshot_internal::existingSubject(body), std::move(capture), kind});
        return Result<void>::success();
    };
    for (auto handle : units_) {
        auto* body = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (!body) return reject(DiagnosticCode::StaleHandle, "Owned unit retired");
        auto frozen = captureBody(*body);
        if (!frozen) return R::failure(frozen.status());
    }
    for (auto handle : buildings_) {
        auto* body = dynamic_cast<Building*>(ecs::try_get(handle));
        if (!body) return reject(DiagnosticCode::StaleHandle, "Owned building retired");
        auto frozen = captureBody(*body);
        if (!frozen) return R::failure(frozen.status());
    }
    std::sort(result->members.begin(), result->members.end(),
              [](const auto& a, const auto& b) { return a.body.persistentId() < b.body.persistentId(); });
    return R::success(RTSWeaponCapture(std::move(result)));
}
}  // namespace eve::rts
