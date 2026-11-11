#include "rts/RTSAttributeCapture.h"
#include <algorithm>
#include <map>
#include "attributes/AttributeProjectionCapture.h"
#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"
namespace eve::rts {
namespace {
bool same(ecs::EntityHandle a, ecs::EntityHandle b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
template <class T>
Result<T> reject(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message));
}
ResultRef<AttributeComponent> projection(ecs::EntityHandle handle, SubjectRef subject) {
    auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
    if (!unit || snapshot_internal::existingSubject(*unit) != subject)
        return reject<std::reference_wrapper<AttributeComponent>>(DiagnosticCode::StaleHandle,
                                                                  "Unit identity or incarnation changed");
    auto* component = detail::existingComponent<Unit::Attributes>(*unit);
    if (!component || !same(component->values.owner(), handle))
        return reject<std::reference_wrapper<AttributeComponent>>(DiagnosticCode::Conflict,
                                                                  "Unit attributes missing or foreign-owned");
    return ResultRef<AttributeComponent>::success(std::ref(component->values));
}
}  // namespace
struct RTSUnitAttributeCapture::Impl {
    PersistentId                        epoch;
    std::vector<RTSUnitAttributeMember> members;
};
RTSUnitAttributeCapture::RTSUnitAttributeCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSUnitAttributeCapture::~RTSUnitAttributeCapture()                                             = default;
RTSUnitAttributeCapture::RTSUnitAttributeCapture(RTSUnitAttributeCapture&&) noexcept            = default;
RTSUnitAttributeCapture& RTSUnitAttributeCapture::operator=(RTSUnitAttributeCapture&&) noexcept = default;
PersistentId RTSUnitAttributeCapture::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::span<const RTSUnitAttributeMember> RTSUnitAttributeCapture::members() const noexcept {
    return impl_ ? std::span<const RTSUnitAttributeMember>(impl_->members) : std::span<const RTSUnitAttributeMember>{};
}
Result<SnapshotEnvelope> RTSUnitAttributeCapture::seal(Revision revision, SimulationTick tick,
                                                       const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                            "Unit attribute sealing requires a live capture and nonzero revision"));
    Value::Array rows;
    rows.reserve(impl_->members.size());
    for (const auto& member : impl_->members) {
        if (!member.attributes)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Missing Unit projection"));
        auto values = member.attributes->encodeState();
        if (!values) return R::failure(values.status());
        rows.push_back(Value::Array{member.subject.format(), std::move(values).takeValue()});
    }
    return makeSnapshotEnvelope("eve.rts-unit-attributes", *LogicalId::fromParts("rts", "unit-attributes"),
                                SchemaVersion(1), impl_->epoch, revision, tick,
                                Value::object({{"members", Value(std::move(rows))}}), hash);
}
Result<RTSUnitAttributeCapture> RTSUnitAttributeCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                               const SnapshotHashProvider& hash) {
    using R       = Result<RTSUnitAttributeCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown Unit attribute version"));
    if (snapshot.type != "eve.rts-unit-attributes" ||
        snapshot.schema != *LogicalId::fromParts("rts", "unit-attributes") || epoch.isNil() ||
        snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Unit attribute boundary mismatch"));
    const auto* root = snapshot.payload.getIf<Value::Object>();
    if (!root || root->size() != 1 || !root->contains("members") || !root->at("members").isArray())
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unknown or missing Unit attribute fields"));
    auto result   = std::make_unique<Impl>();
    result->epoch = epoch;
    for (const auto& row : *root->at("members").getIf<Value::Array>()) {
        const auto* tuple = row.getIf<Value::Array>();
        if (!tuple || tuple->size() != 2 || !(*tuple)[0].isString())
            return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid Unit attribute member"));
        const auto& text = *(*tuple)[0].getIf<std::string>();
        const auto  id   = PersistentId::parse(text);
        if (!id || id->isNil() || id->format() != text ||
            (!result->members.empty() && !(result->members.back().subject.persistentId() < *id)))
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                "Unit attribute identities must be canonical unique and ordered"));
        auto values = attributes::AttributeProjectionCapture::prepareState((*tuple)[1]);
        if (!values) return R::failure(values.status());
        result->members.push_back(
            {SubjectRef::fromPersistentId(*id),
             std::make_shared<const attributes::AttributeProjectionCapture>(std::move(values).takeValue())});
    }
    return R::success(RTSUnitAttributeCapture(std::move(result)));
}
struct PreparedRTSUnitAttributeRestore::Impl {
    /** @ownership Observed facade, guarded by physical lifetime and locked world before use.
     * @lifetime Never dereferenced by token destruction; publication requires this exact live target.
     */
    RTS*                           target;
    LifetimeWitness                lifetime;
    std::weak_ptr<ecs::Table>      world;
    std::vector<ecs::EntityHandle> topology;
    struct Member {
        ecs::EntityHandle                       target;
        SubjectRef                              subject;
        attributes::PreparedAttributeProjection attributes;
    };
    std::vector<Member> members;
};
PreparedRTSUnitAttributeRestore::PreparedRTSUnitAttributeRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSUnitAttributeRestore::~PreparedRTSUnitAttributeRestore()                                          = default;
PreparedRTSUnitAttributeRestore::PreparedRTSUnitAttributeRestore(PreparedRTSUnitAttributeRestore&&) noexcept = default;
PreparedRTSUnitAttributeRestore& PreparedRTSUnitAttributeRestore::operator=(
    PreparedRTSUnitAttributeRestore&&) noexcept = default;
Result<RTSUnitAttributeCapture> RTS::captureUnitAttributes(PersistentId epoch) const {
    using R = Result<RTSUnitAttributeCapture>;
    if (epoch.isNil())
        return reject<RTSUnitAttributeCapture>(DiagnosticCode::InvalidArgument, "Attribute scope needs save epoch");
    if (!ownedWorld_)
        return reject<RTSUnitAttributeCapture>(DiagnosticCode::Unsupported, "Attribute scope needs owned Table");
    if (ownedWorld_->is_deferred())
        return reject<RTSUnitAttributeCapture>(DiagnosticCode::PreconditionViolation,
                                               "Attribute scope needs idle Table");
    ecs::ScopedTable scope(*ownedWorld_);
    auto             result = std::make_unique<RTSUnitAttributeCapture::Impl>();
    result->epoch           = epoch;
    result->members.reserve(units_.size());
    for (auto handle : units_) {
        if (handle.table != ownedWorld_.get())
            return reject<RTSUnitAttributeCapture>(DiagnosticCode::Conflict, "Foreign unit Table");
        auto subject = snapshot_internal::subjectOf(handle);
        if (!subject.isValid())
            return reject<RTSUnitAttributeCapture>(DiagnosticCode::StaleHandle, "Unit identity missing");
        auto source = projection(handle, subject);
        if (!source) return R::failure(source.status());
        auto captured = source.value().get().capture();
        if (!captured) return R::failure(captured.status());
        result->members.push_back(
            {subject, std::make_shared<const attributes::AttributeProjectionCapture>(std::move(captured).takeValue())});
    }
    std::sort(result->members.begin(), result->members.end(),
              [](const auto& a, const auto& b) { return a.subject.persistentId() < b.subject.persistentId(); });
    for (std::size_t i = 1; i < result->members.size(); ++i)
        if (result->members[i - 1].subject == result->members[i].subject)
            return reject<RTSUnitAttributeCapture>(DiagnosticCode::Conflict, "Duplicate Unit subject");
    return R::success(RTSUnitAttributeCapture(std::move(result)));
}
Result<PreparedRTSUnitAttributeRestore> PreparedRTSUnitAttributeRestore::prepare(
    RTS& target, const RTSUnitAttributeCapture& captured) {
    using R = Result<PreparedRTSUnitAttributeRestore>;
    if (captured.saveEpoch().isNil())
        return reject<PreparedRTSUnitAttributeRestore>(DiagnosticCode::InvalidArgument, "Attribute capture moved");
    auto world = target.ownedWorld_;
    if (!world)
        return reject<PreparedRTSUnitAttributeRestore>(DiagnosticCode::Unsupported,
                                                       "Attribute restore needs owned Table");
    if (world->is_deferred())
        return reject<PreparedRTSUnitAttributeRestore>(DiagnosticCode::PreconditionViolation,
                                                       "Attribute restore needs idle Table");
    if (target.units_.size() != captured.members().size())
        return reject<PreparedRTSUnitAttributeRestore>(DiagnosticCode::Conflict, "Unit attribute topology differs");
    ecs::ScopedTable                          scope(*world);
    std::map<PersistentId, ecs::EntityHandle> targets;
    for (auto handle : target.units_) {
        auto subject = snapshot_internal::subjectOf(handle);
        if (handle.table != world.get() || !subject.isValid() ||
            !targets.emplace(subject.persistentId(), handle).second)
            return reject<PreparedRTSUnitAttributeRestore>(DiagnosticCode::Conflict, "Invalid target Unit topology");
    }
    auto plan      = std::make_unique<Impl>();
    plan->target   = &target;
    plan->lifetime = target.lifetimeWitness();
    plan->world    = world;
    plan->topology = target.units_;
    plan->members.reserve(target.units_.size());
    for (const auto& member : captured.members()) {
        auto found = targets.find(member.subject.persistentId());
        if (found == targets.end() || !member.attributes)
            return reject<PreparedRTSUnitAttributeRestore>(DiagnosticCode::Conflict, "Unit attribute member missing");
        auto destination = projection(found->second, member.subject);
        if (!destination) return R::failure(destination.status());
        auto prepared = attributes::PreparedAttributeProjection::prepare(*member.attributes);
        if (!prepared) return R::failure(prepared.status());
        auto valid = destination.value().get().validateRestore(prepared.value(), found->second);
        if (!valid) return R::failure(valid.status());
        plan->members.push_back({found->second, member.subject, std::move(prepared).takeValue()});
    }
    return R::success(PreparedRTSUnitAttributeRestore(std::move(plan)));
}
Result<void> RTS::restoreUnitAttributes(PreparedRTSUnitAttributeRestore&& prepared) {
    auto* plan = prepared.impl_.get();
    if (!plan || plan->target != this || !plan->lifetime.isAlive())
        return reject<void>(DiagnosticCode::PreconditionViolation, "Attribute token consumed or foreign");
    auto world = plan->world.lock();
    if (!world || world != ownedWorld_ || world->is_deferred())
        return reject<void>(DiagnosticCode::PreconditionViolation, "Attribute target world retired or busy");
    if (units_.size() != plan->topology.size())
        return reject<void>(DiagnosticCode::Conflict, "Unit topology changed after preparation");
    for (std::size_t i = 0; i < units_.size(); ++i)
        if (!same(units_[i], plan->topology[i]))
            return reject<void>(DiagnosticCode::StaleHandle, "Unit topology incarnation changed");
    ecs::ScopedTable scope(*world);
    for (const auto& member : plan->members) {
        auto destination = projection(member.target, member.subject);
        if (!destination) return Result<void>::failure(destination.status());
        auto valid = destination.value().get().validateRestore(member.attributes, member.target);
        if (!valid) return Result<void>::failure(valid.status());
    }
    const bool empty = plan->members.empty();
    for (auto& member : plan->members) {
        auto* unit = static_cast<Unit*>(ecs::try_get(member.target));
        detail::existingComponent<Unit::Attributes>(*unit)
            ->values.restore(std::move(member.attributes), member.target)
            .expect("prevalidated native Unit attribute publication");
    }
    prepared.impl_.reset();
    return Result<void>::success(Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}
}  // namespace eve::rts
