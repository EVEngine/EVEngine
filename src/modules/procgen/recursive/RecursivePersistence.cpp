#include "procgen/recursive/RecursivePersistence.h"

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace eve::procgen::recursive {
namespace {
constexpr std::string_view schemaName   = "eve:recursive-model";
constexpr std::string_view snapshotType = "recursive-model";
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "procgen.recursive.snapshot"));
}
Value vectorValue(const std::array<double, 3>& vector) {
    return Value::array({Value(vector[0]), Value(vector[1]), Value(vector[2])});
}
bool exactFields(const Value& value, std::initializer_list<std::string_view> names) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || object->size() != names.size()) return false;
    for (auto name : names)
        if (!object->contains(std::string(name))) return false;
    return true;
}
Result<std::uint64_t> unsignedValue(const Value& value) {
    const auto* text = value.getIf<std::string>();
    if (!text || text->empty() || text->size() > 20)
        return fail<std::uint64_t>(DiagnosticCode::ParseError, "Unsigned values require bounded decimal strings");
    std::uint64_t result    = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
    if (error != std::errc{} || end != text->data() + text->size())
        return fail<std::uint64_t>(DiagnosticCode::ParseError, "Invalid unsigned decimal value");
    return Result<std::uint64_t>::success(result);
}
Result<double> number(const Value& value) {
    if (!value.isInt64() && !value.isDouble())
        return fail<double>(DiagnosticCode::ParseError, "Expected numeric value");
    const double result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(result)) return fail<double>(DiagnosticCode::ParseError, "Expected finite numeric value");
    return Result<double>::success(result);
}
Result<std::array<double, 3>> vectorNumber(const Value& value) {
    if (!value.isArray() || value.arraySize() != 3)
        return fail<std::array<double, 3>>(DiagnosticCode::ParseError, "Expected three coordinates");
    std::array<double, 3> result{};
    for (unsigned i = 0; i < 3; ++i) {
        auto coordinate = number(value.at(i));
        if (!coordinate.ok()) return Result<std::array<double, 3>>::failure(coordinate.status());
        result[i] = coordinate.value();
    }
    return Result<std::array<double, 3>>::success(result);
}
Value encode(const RecursiveState& state) {
    const auto&  d          = state.definition;
    auto         definition = Value::object({{"ruleVersion", Value(std::to_string(d.ruleVersion))},
                                             {"seed", Value(std::to_string(d.seed))},
                                             {"capacity", Value(std::to_string(d.capacity))},
                                             {"baseHalfExtent", vectorValue(d.baseHalfExtent)},
                                             {"lengthExponent", Value(int64_t{d.lengthExponent})},
                                             {"density", Value(d.density)},
                                             {"capacityMode", Value(static_cast<int64_t>(d.capacityMode))},
                                             {"definitionRevision", Value(std::to_string(d.revision.value()))}});
    Value::Array edits;
    edits.reserve(state.edits.size());
    for (const auto& edit : state.edits)
        edits.push_back(Value::object({{"ordinal", Value(std::to_string(edit.ordinal))},
                                       {"massScale", Value(edit.massScale)},
                                       {"displacement", vectorValue(edit.displacement)},
                                       {"velocity", vectorValue(edit.velocity)}}));
    return Value::object({{"definition", std::move(definition)}, {"edits", Value(std::move(edits))}});
}
Result<RecursiveState> decode(const SnapshotEnvelope& snapshot, EditBudget budget) {
    if (!exactFields(snapshot.payload, {"definition", "edits"}))
        return fail<RecursiveState>(DiagnosticCode::ParseError, "Unknown or missing payload fields");
    const auto& payload    = *snapshot.payload.getIf<Value::Object>();
    const auto& definition = payload.at("definition");
    if (!exactFields(definition, {"ruleVersion", "seed", "capacity", "baseHalfExtent", "lengthExponent", "density",
                                  "capacityMode", "definitionRevision"}))
        return fail<RecursiveState>(DiagnosticCode::ParseError, "Unknown or missing definition fields");
    const auto&    d = *definition.getIf<Value::Object>();
    RecursiveState state;
    state.revision = snapshot.revision;
    auto rule      = unsignedValue(d.at("ruleVersion"));
    if (!rule.ok()) return Result<RecursiveState>::failure(rule.status());
    auto seed = unsignedValue(d.at("seed"));
    if (!seed.ok()) return Result<RecursiveState>::failure(seed.status());
    auto capacity = unsignedValue(d.at("capacity"));
    if (!capacity.ok()) return Result<RecursiveState>::failure(capacity.status());
    auto revision = unsignedValue(d.at("definitionRevision"));
    if (!revision.ok()) return Result<RecursiveState>::failure(revision.status());
    if (rule.value() > UINT32_MAX || !d.at("lengthExponent").isInt64() || !d.at("capacityMode").isInt64())
        return fail<RecursiveState>(DiagnosticCode::ParseError, "Invalid rule, exponent or capacity mode type");
    const auto exponent = d.at("lengthExponent").asInt();
    const auto mode     = d.at("capacityMode").asInt();
    if (exponent < -900 || exponent > 900 || mode < 0 || mode > 1)
        return fail<RecursiveState>(DiagnosticCode::Unsupported, "Invalid physical scale or capacity mode");
    auto half = vectorNumber(d.at("baseHalfExtent"));
    if (!half.ok()) return Result<RecursiveState>::failure(half.status());
    auto density = number(d.at("density"));
    if (!density.ok()) return Result<RecursiveState>::failure(density.status());
    state.definition  = {static_cast<std::uint32_t>(rule.value()),
                         seed.value(),
                         capacity.value(),
                         half.value(),
                         static_cast<int>(exponent),
                         density.value(),
                         static_cast<CapacityMode>(mode),
                         Revision{revision.value()}};
    const auto& edits = payload.at("edits");
    if (!edits.isArray() || edits.arraySize() > budget.maxEdits)
        return fail<RecursiveState>(DiagnosticCode::ParseError, "Invalid or over-budget edit array");
    state.edits.reserve(edits.arraySize());
    for (std::size_t i = 0; i < edits.arraySize(); ++i) {
        const auto& entry = edits.at(i);
        if (!exactFields(entry, {"ordinal", "massScale", "displacement", "velocity"}))
            return fail<RecursiveState>(DiagnosticCode::ParseError, "Unknown or missing edit fields");
        const auto& e       = *entry.getIf<Value::Object>();
        auto        ordinal = unsignedValue(e.at("ordinal"));
        if (!ordinal.ok()) return Result<RecursiveState>::failure(ordinal.status());
        auto mass = number(e.at("massScale"));
        if (!mass.ok()) return Result<RecursiveState>::failure(mass.status());
        auto displacement = vectorNumber(e.at("displacement"));
        if (!displacement.ok()) return Result<RecursiveState>::failure(displacement.status());
        auto velocity = vectorNumber(e.at("velocity"));
        if (!velocity.ok()) return Result<RecursiveState>::failure(velocity.status());
        state.edits.push_back({ordinal.value(), mass.value(), displacement.value(), velocity.value()});
    }
    return Result<RecursiveState>::success(std::move(state));
}
}  // namespace
Result<SnapshotEnvelope> saveRecursiveModel(const RecursiveInstance& instance, PersistentId instanceId,
                                            SimulationTick tick, const SnapshotHashProvider& hasher) {
    if (instanceId.isNil())
        return fail<SnapshotEnvelope>(DiagnosticCode::InvalidArgument, "Persistent instance identity is required");
    auto state = instance.capture();
    if (!state.ok()) return Result<SnapshotEnvelope>::failure(state.status());
    try {
        const auto schema = LogicalId::parse(schemaName);
        if (!schema)
            return fail<SnapshotEnvelope>(DiagnosticCode::InvariantViolation, "Invalid built-in schema identifier");
        return makeSnapshotEnvelope(std::string(snapshotType), *schema, SchemaVersion{1}, instanceId,
                                    state.value().revision, tick, encode(state.value()), hasher);
    } catch (const std::bad_alloc&) {
        return fail<SnapshotEnvelope>(DiagnosticCode::Failed, "Snapshot capture allocation failed");
    }
}
Result<RestoredRecursiveModel> restoreRecursiveModel(const SnapshotEnvelope& snapshot, EditBudget budget,
                                                     const SnapshotHashProvider& hasher) {
    if (snapshot.type != snapshotType || snapshot.schema.format() != schemaName ||
        snapshot.schemaVersion != SchemaVersion{1})
        return fail<RestoredRecursiveModel>(DiagnosticCode::UnknownVersion,
                                            "Unsupported recursive snapshot schema/version");
    if (snapshot.instanceId.isNil())
        return fail<RestoredRecursiveModel>(DiagnosticCode::InvalidArgument,
                                            "Persistent instance identity is required");
    if (budget.maxEdits == 0 || budget.maxEdits > 1000000 || budget.maxSummaries == 0 || budget.maxSummaries > 2000000)
        return fail<RestoredRecursiveModel>(DiagnosticCode::InvalidArgument, "Invalid restore resource limits");
    try {
        auto state = decode(snapshot, budget);
        if (!state.ok()) return Result<RestoredRecursiveModel>::failure(state.status());
        auto verified = verifySnapshotEnvelope(snapshot, hasher);
        if (!verified.ok()) return Result<RestoredRecursiveModel>::failure(verified.status());
        auto restored = RecursiveInstance::restoreState(state.value(), budget);
        if (!restored.ok()) return Result<RestoredRecursiveModel>::failure(restored.status());
        return Result<RestoredRecursiveModel>::success(
            {std::move(restored.value()), snapshot.instanceId, snapshot.tick});
    } catch (const std::bad_alloc&) {
        return fail<RestoredRecursiveModel>(DiagnosticCode::Failed, "Snapshot restore allocation failed");
    }
}
}  // namespace eve::procgen::recursive
