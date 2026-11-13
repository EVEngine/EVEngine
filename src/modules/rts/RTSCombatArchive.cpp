#include "rts/RTSCombatArchive.h"
#include "rts/RTS.h"
#include "rts/RTSCombatStateInternal.h"
namespace eve::rts {
namespace {
LogicalId schema() { return *LogicalId::fromParts("rts", "combat-state"); }
Value     encode(const RTSCombatSnapshot& state) {
    Value::Array mirrors, blocked;
    for (const auto& id : state.mirroredSubjects) mirrors.emplace_back(id);
    for (const auto& id : state.blockedSubjects) blocked.emplace_back(id);
    return Value::object({{"mirrored", Value(std::move(mirrors))}, {"blocked", Value(std::move(blocked))}});
}
Result<RTSCombatSnapshot> decode(const Value& value) {
    using R          = Result<RTSCombatSnapshot>;
    const auto* root = value.getIf<Value::Object>();
    if (!root || root->size() != 2 || !root->contains("mirrored") || !root->contains("blocked"))
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unknown or missing combat fields"));
    RTSCombatSnapshot state;
    for (const auto* key : {"mirrored", "blocked"}) {
        const auto* rows = root->at(key).getIf<Value::Array>();
        if (!rows) return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Expected combat identity array"));
        auto& result = std::string_view(key) == "mirrored" ? state.mirroredSubjects : state.blockedSubjects;
        for (const auto& row : *rows) {
            const auto* text = row.getIf<std::string>();
            if (!text || !result.insert(*text).second)
                return R::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "Invalid or duplicate combat identity"));
        }
    }
    auto valid = combat_state_internal::validate(state);
    if (!valid) return R::failure(valid.status());
    return R::success(std::move(state));
}
}  // namespace
struct RTSCombatArchive::Data {
    RTSCombatSnapshot state;
    PersistentId      epoch;
    Revision          revision;
    SimulationTick    tick;
};
RTSCombatArchive::RTSCombatArchive(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
RTSCombatArchive::~RTSCombatArchive()                                                         = default;
RTSCombatArchive::RTSCombatArchive(RTSCombatArchive&&) noexcept                               = default;
RTSCombatArchive&                    RTSCombatArchive::operator=(RTSCombatArchive&&) noexcept = default;
OptionalRef<const RTSCombatSnapshot> RTSCombatArchive::state() const noexcept {
    return data_ ? OptionalRef<const RTSCombatSnapshot>(std::cref(data_->state)) : std::nullopt;
}
PersistentId             RTSCombatArchive::saveEpoch() const noexcept { return data_ ? data_->epoch : PersistentId{}; }
Revision                 RTSCombatArchive::revision() const noexcept { return data_ ? data_->revision : Revision{}; }
SimulationTick           RTSCombatArchive::tick() const noexcept { return data_ ? data_->tick : SimulationTick{}; }
Result<RTSCombatArchive> RTSCombatArchive::capture(RTSCombatSnapshot state, PersistentId epoch, Revision revision,
                                                   SimulationTick tick) {
    if (epoch.isNil() || revision.isZero())
        return Result<RTSCombatArchive>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Combat requires epoch/revision"));
    auto valid = combat_state_internal::validate(state);
    if (!valid) return Result<RTSCombatArchive>::failure(valid.status());
    return Result<RTSCombatArchive>::success(
        RTSCombatArchive(std::make_shared<const Data>(Data{std::move(state), epoch, revision, tick})));
}
Result<SnapshotEnvelope> RTSCombatArchive::seal(const SnapshotHashProvider& hash) const {
    auto data = data_;
    if (!data)
        return Result<SnapshotEnvelope>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Combat archive was moved"));
    return makeSnapshotEnvelope("eve.rts-combat", schema(), SchemaVersion(1), data->epoch, data->revision, data->tick,
                                encode(data->state), hash);
}
Result<RTSCombatArchive> RTSCombatArchive::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                 const SnapshotHashProvider& hash) {
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return Result<RTSCombatArchive>::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return Result<RTSCombatArchive>::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RTS combat version"));
    if (snapshot.type != "eve.rts-combat" || snapshot.schema != schema() || epoch.isNil() ||
        snapshot.instanceId != epoch || snapshot.revision.isZero())
        return Result<RTSCombatArchive>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RTS combat boundary mismatch"));
    auto state = decode(snapshot.payload);
    if (!state) return Result<RTSCombatArchive>::failure(state.status());
    return Result<RTSCombatArchive>::success(RTSCombatArchive(
        std::make_shared<const Data>(Data{std::move(state).takeValue(), epoch, snapshot.revision, snapshot.tick})));
}
}  // namespace eve::rts
