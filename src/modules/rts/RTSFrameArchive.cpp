#include "rts/RTSFrameArchive.h"
#include <charconv>
#include <cmath>
#include <limits>
#include "rts/RTS.h"
#include "rts/RTSFrameRestore.h"
namespace eve::rts {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "rts.frame-state"))};
}
template <class T>
T admitted(Result<T> result) {
    if (!result) throw Invalid{result.status()};
    return std::move(result).takeValue();
}
const Value::Array& array(const Value& value) {
    auto* result = value.getIf<Value::Array>();
    if (!result) invalid("Expected frame array");
    return *result;
}
const Value::Array& tuple(const Value& value, std::size_t size) {
    const auto& result = array(value);
    if (result.size() != size) invalid("Unknown or missing frame tuple fields");
    return result;
}
const std::string& text(const Value& value) {
    auto* result = value.getIf<std::string>();
    if (!result) invalid("Expected frame text");
    return *result;
}
std::uint64_t counter(const Value& value) {
    const auto&   source = text(value);
    std::uint64_t result = 0;
    auto          parsed = std::from_chars(source.data(), source.data() + source.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size() || std::to_string(result) != source)
        invalid("Invalid canonical frame counter");
    return result;
}
bool boolean(const Value& value) {
    auto* result = value.getIf<bool>();
    if (!result) invalid("Expected frame boolean");
    return *result;
}
double number(const Value& value, bool positive = false) {
    if (!value.isDouble() && !value.isInt64()) invalid("Expected frame number");
    const auto result = value.isDouble() ? value.asDouble() : static_cast<double>(value.asInt());
    if (!std::isfinite(result) || (positive && result < 0)) invalid("Invalid frame number range");
    return result;
}
float coordinate(const Value& value) {
    const auto result = number(value);
    if (std::abs(result) > std::numeric_limits<float>::max()) invalid("Frame position exceeds float range");
    return static_cast<float>(result);
}
int enumeration(const Value& value, int maximum) {
    if (!value.isInt64() || value.asInt() < 0 || value.asInt() > maximum) invalid("Unknown frame enum");
    return static_cast<int>(value.asInt());
}
SubjectRef subject(const Value& value) {
    const auto id = PersistentId::parse(text(value));
    if (!id) invalid("Invalid historical subject identity");
    return SubjectRef::fromPersistentId(*id);
}
Value            impulse(combat::Impulse3 value) { return Value::Array{value.x, value.y, value.z}; }
combat::Impulse3 impulse(const Value& value) {
    const auto& row = tuple(value, 3);
    return {number(row[0]), number(row[1]), number(row[2])};
}
Value encodeRequest(const combat::DamageRequest& request) {
    Value action;
    if (request.actionExecution) action = std::to_string(request.actionExecution->value());
    return Value::Array{request.source.format(),
                        request.target.format(),
                        std::move(action),
                        request.damageType,
                        request.healthDamage,
                        request.poiseDamage,
                        request.incomingDamageMultiplier,
                        request.availableShield,
                        impulse(request.knockback),
                        std::to_string(request.tick.value()),
                        request.critical};
}
combat::DamageRequest decodeRequest(const Value& value) {
    const auto&           row = tuple(value, 11);
    combat::DamageRequest request;
    request.source = subject(row[0]);
    request.target = subject(row[1]);
    if (!row[2].isNull()) request.actionExecution = action::ActionExecutionId(counter(row[2]));
    request.damageType               = text(row[3]);
    request.healthDamage             = number(row[4], true);
    request.poiseDamage              = number(row[5], true);
    request.incomingDamageMultiplier = number(row[6], true);
    request.availableShield          = number(row[7], true);
    request.knockback                = impulse(row[8]);
    request.tick                     = SimulationTick(counter(row[9]));
    request.critical                 = boolean(row[10]);
    auto valid                       = request.validate();
    if (!valid) throw Invalid{valid.status()};
    return request;
}
Value encodeOutcome(const combat::DamageOutcome& outcome) {
    Value transport;
    if (outcome.settlementResult.event)
        transport = Value::Array{outcome.settlementResult.event->eventId.format(),
                                 std::to_string(outcome.settlementResult.event->sequence.value())};
    return Value::Array{
        outcome.source.format(),
        outcome.target.format(),
        outcome.previousHealth,
        outcome.health,
        outcome.previousPoise,
        outcome.poise,
        outcome.appliedHealthDamage,
        outcome.appliedPoiseDamage,
        outcome.absorbedShieldDamage,
        impulse(outcome.knockback),
        int(outcome.reaction),
        int(outcome.ruleSource),
        admitted(Value::fromJson(admitted(settlement::settlementRequestCanonicalJson(outcome.settlementRequest)))),
        admitted(Value::fromJson(admitted(settlement::settlementResultCanonicalJson(outcome.settlementResult)))),
        std::move(transport)};
}
combat::DamageOutcome decodeOutcome(const Value& value) {
    const auto&           row = tuple(value, 15);
    combat::DamageOutcome outcome;
    outcome.source               = subject(row[0]);
    outcome.target               = subject(row[1]);
    outcome.previousHealth       = number(row[2], true);
    outcome.health               = number(row[3], true);
    outcome.previousPoise        = number(row[4], true);
    outcome.poise                = number(row[5], true);
    outcome.appliedHealthDamage  = number(row[6], true);
    outcome.appliedPoiseDamage   = number(row[7], true);
    outcome.absorbedShieldDamage = number(row[8], true);
    outcome.knockback            = impulse(row[9]);
    outcome.reaction = static_cast<combat::HitReaction>(enumeration(row[10], int(combat::HitReaction::Death)));
    outcome.ruleSource =
        static_cast<combat::DamageRuleSource>(enumeration(row[11], int(combat::DamageRuleSource::Provider)));
    outcome.settlementRequest = admitted(settlement::settlementRequestFromJson(admitted(row[12].toJson())));
    outcome.settlementResult  = admitted(settlement::settlementResultFromJson(admitted(row[13].toJson())));
    if (outcome.settlementResult.event) {
        const auto& transport = tuple(row[14], 2);
        auto        id        = game_event::EventId::parse(text(transport[0]));
        if (!id) invalid("Invalid historical event transport identity");
        outcome.settlementResult.event->eventId  = *id;
        outcome.settlementResult.event->sequence = EventSequence(counter(transport[1]));
    } else if (!row[14].isNull())
        invalid("Transport metadata without semantic event");
    return outcome;
}
Value damageEvent(const RTSFrameDamageEvent& row) {
    return Value::Array{std::to_string(row.sequence), std::to_string(row.tick.value()), int(row.channel),
                        encodeRequest(row.request), encodeOutcome(row.outcome)};
}
RTSFrameDamageEvent damageEvent(const Value& value) {
    const auto&         row = tuple(value, 5);
    RTSFrameDamageEvent event{counter(row[0]), SimulationTick(counter(row[1])),
                              static_cast<DamageChannel>(enumeration(row[2], int(DamageChannel::Weapon))),
                              decodeRequest(row[3]), decodeOutcome(row[4])};
    if (event.request.source != event.outcome.source || event.request.target != event.outcome.target ||
        event.outcome.settlementRequest.source != event.outcome.source ||
        event.outcome.settlementRequest.target != event.outcome.target)
        invalid("Damage request/outcome/audit identities disagree");
    return event;
}
Value encode(const RTSFrameState& state) {
    Value::Array damage, combat, lifecycle, gameplay;
    for (const auto& row : state.damage) damage.push_back(damageEvent(row));
    for (const auto& row : state.combat)
        combat.push_back(Value::Array{std::to_string(row.sequence), std::to_string(row.tick.value()),
                                      int(row.event.kind), row.event.source.format(), row.event.target.format(),
                                      Value::Array{double(row.event.point.x), double(row.event.point.y)}});
    for (const auto& row : state.lifecycle)
        lifecycle.push_back(Value::Array{std::to_string(row.sequence), std::to_string(row.tick.value()),
                                         int(row.event.kind), row.event.source.format(), row.event.target.format(),
                                         row.event.detail, row.event.value});
    for (const auto& row : state.gameplay)
        gameplay.push_back(Value::Array{std::to_string(row.sequence), std::to_string(row.tick.value()), row.type,
                                        row.subject.format(), row.causationCommandId, row.correlationId, row.payload});
    return Value::object({{"damage", Value(std::move(damage))},
                          {"combat", Value(std::move(combat))},
                          {"lifecycle", Value(std::move(lifecycle))},
                          {"gameplay", Value(std::move(gameplay))},
                          {"sequence", std::to_string(state.sequence)},
                          {"preserve", state.preserveOnNextStep},
                          {"next_gameplay", std::to_string(state.nextGameplaySequence)}});
}
RTSFrameState decode(const Value& value) {
    const auto* root = value.getIf<Value::Object>();
    if (!root || root->size() != 7) invalid("Unknown or missing frame payload fields");
    for (const auto* key : {"damage", "combat", "lifecycle", "gameplay", "sequence", "preserve", "next_gameplay"})
        if (!root->contains(key)) invalid("Missing frame field");
    RTSFrameState result;
    result.sequence             = counter(root->at("sequence"));
    result.preserveOnNextStep   = boolean(root->at("preserve"));
    result.nextGameplaySequence = counter(root->at("next_gameplay"));
    for (const auto& value : array(root->at("damage"))) result.damage.push_back(damageEvent(value));
    for (const auto& value : array(root->at("combat"))) {
        const auto& row   = tuple(value, 6);
        const auto& point = tuple(row[5], 2);
        result.combat.push_back(
            {counter(row[0]),
             SimulationTick(counter(row[1])),
             {static_cast<CombatFireEventKind>(enumeration(row[2], int(CombatFireEventKind::FireBlocked))),
              subject(row[3]),
              subject(row[4]),
              {coordinate(point[0]), coordinate(point[1])}}});
    }
    for (const auto& value : array(root->at("lifecycle"))) {
        const auto& row = tuple(value, 7);
        result.lifecycle.push_back(
            {counter(row[0]),
             SimulationTick(counter(row[1])),
             {static_cast<LifecycleEventKind>(enumeration(row[2], int(LifecycleEventKind::SupplyReturned))),
              subject(row[3]), subject(row[4]), text(row[5]), number(row[6])}});
    }
    for (const auto& value : array(root->at("gameplay"))) {
        const auto& row     = tuple(value, 7);
        auto        payload = row[6].toJson();
        if (!payload) throw Invalid{payload.status()};
        result.gameplay.push_back({counter(row[0]), SimulationTick(counter(row[1])), text(row[2]), subject(row[3]),
                                   text(row[4]), text(row[5]), row[6]});
    }
    auto valid = PreparedRTSFrameRestore::prepare(result);
    if (!valid) throw Invalid{valid.status()};
    return result;
}
LogicalId schema() { return *LogicalId::fromParts("rts", "frame-state"); }
}  // namespace
struct RTSFrameArchive::Data {
    RTSFrameState  state;
    PersistentId   epoch;
    Revision       revision;
    SimulationTick tick;
};
RTSFrameArchive::RTSFrameArchive(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
RTSFrameArchive::~RTSFrameArchive()                                                     = default;
RTSFrameArchive::RTSFrameArchive(RTSFrameArchive&&) noexcept                            = default;
RTSFrameArchive&                 RTSFrameArchive::operator=(RTSFrameArchive&&) noexcept = default;
OptionalRef<const RTSFrameState> RTSFrameArchive::state() const noexcept {
    return data_ ? OptionalRef<const RTSFrameState>(std::cref(data_->state)) : std::nullopt;
}
PersistentId   RTSFrameArchive::saveEpoch() const noexcept { return data_ ? data_->epoch : PersistentId{}; }
Revision       RTSFrameArchive::revision() const noexcept { return data_ ? data_->revision : Revision{}; }
SimulationTick RTSFrameArchive::tick() const noexcept { return data_ ? data_->tick : SimulationTick{}; }
Result<Value>  RTSFrameArchive::encodeDamageInputs(std::span<const RTSFrameDamageEvent> inputs) {
    try {
        if (inputs.size() > 1000000) invalid("Too many damage inputs");
        Value::Array rows;
        rows.reserve(inputs.size());
        for (const auto& input : inputs) {
            auto encoded = damageEvent(input);
            (void)damageEvent(encoded);
            rows.push_back(std::move(encoded));
        }
        return Result<Value>::success(std::move(rows));
    } catch (const Invalid& error) {
        return Result<Value>::failure(error.status);
    }
}
Result<std::vector<RTSFrameDamageEvent>> RTSFrameArchive::prepareDamageInputs(const Value& value) {
    using R = Result<std::vector<RTSFrameDamageEvent>>;
    try {
        const auto& rows = array(value);
        if (rows.size() > 1000000) invalid("Too many damage inputs");
        std::vector<RTSFrameDamageEvent> inputs;
        inputs.reserve(rows.size());
        for (const auto& row : rows) inputs.push_back(damageEvent(row));
        return R::success(std::move(inputs));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<RTSFrameArchive> RTSFrameArchive::capture(RTSFrameState state, PersistentId epoch, Revision revision,
                                                 SimulationTick tick) {
    if (epoch.isNil() || revision.isZero())
        return Result<RTSFrameArchive>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Frame requires epoch/revision"));
    try {
        auto validated = decode(encode(state));
        return Result<RTSFrameArchive>::success(
            RTSFrameArchive(std::make_shared<const Data>(Data{std::move(validated), epoch, revision, tick})));
    } catch (const Invalid& error) {
        return Result<RTSFrameArchive>::failure(error.status);
    }
}
Result<SnapshotEnvelope> RTSFrameArchive::seal(const SnapshotHashProvider& hash) const {
    auto data = data_;
    if (!data)
        return Result<SnapshotEnvelope>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Frame archive was moved"));
    try {
        return makeSnapshotEnvelope("eve.rts-frame", schema(), SchemaVersion(1), data->epoch, data->revision,
                                    data->tick, encode(data->state), hash);
    } catch (const Invalid& error) {
        return Result<SnapshotEnvelope>::failure(error.status);
    }
}
Result<RTSFrameArchive> RTSFrameArchive::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                               const SnapshotHashProvider& hash) {
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return Result<RTSFrameArchive>::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return Result<RTSFrameArchive>::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RTS frame version"));
    if (snapshot.type != "eve.rts-frame" || snapshot.schema != schema() || epoch.isNil() ||
        snapshot.instanceId != epoch || snapshot.revision.isZero())
        return Result<RTSFrameArchive>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RTS frame boundary mismatch"));
    try {
        auto state = decode(snapshot.payload);
        return Result<RTSFrameArchive>::success(RTSFrameArchive(
            std::make_shared<const Data>(Data{std::move(state), epoch, snapshot.revision, snapshot.tick})));
    } catch (const Invalid& error) {
        return Result<RTSFrameArchive>::failure(error.status);
    }
}
}  // namespace eve::rts
