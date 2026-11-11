/** @file SpawnSavedState.cpp
 * @brief spawn module-interface: canonical durable lifecycle value codec, no live identity or capability issuance.
 * @remarks Shared by RPG and map/RTS participants; duplicate observations remain ordered data, not a second ledger.
 */
#include "spawn/SpawnSavedState.h"
#include <array>
#include <charconv>
#include <limits>
namespace eve::spawn {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "spawn.lifecycle"))};
}
const Value::Object& object(const Value& v, std::initializer_list<const char*> fields) {
    const auto* o = v.getIf<Value::Object>();
    if (!o || o->size() != fields.size()) invalid("Unknown or missing lifecycle fields");
    for (const auto* key : fields)
        if (!o->contains(key)) invalid("Missing lifecycle field");
    return *o;
}
const std::string& text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected lifecycle text");
    return *s;
}
std::uint64_t counter(const Value& v, std::uint64_t max = std::numeric_limits<std::uint64_t>::max()) {
    const auto&   s = text(v);
    std::uint64_t n = 0;
    auto          p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s || n > max)
        invalid("Invalid lifecycle counter");
    return n;
}
PersistentId identity(const Value& v) {
    const auto& s  = text(v);
    auto        id = PersistentId::parse(s);
    if (!id || id->isNil() || id->format() != s) invalid("Invalid lifecycle identity");
    return *id;
}
void validateSource(const SavedSpawnerKey& key) {
    if (key.owner.isNil() || !key.epoch || !key.generation) invalid("Invalid durable source address");
}
Value encodeSource(const SavedSpawnerKey& key) {
    validateSource(key);
    return Value::object({{"owner", key.owner.format()},
                          {"epoch", std::to_string(key.epoch)},
                          {"slot", std::to_string(key.slot)},
                          {"generation", std::to_string(key.generation)}});
}
SavedSpawnerKey decodeSource(const Value& value) {
    const auto&     s = object(value, {"owner", "epoch", "slot", "generation"});
    SavedSpawnerKey key{identity(s.at("owner")), counter(s.at("epoch")),
                        std::uint32_t(counter(s.at("slot"), UINT32_MAX)),
                        std::uint32_t(counter(s.at("generation"), UINT32_MAX))};
    validateSource(key);
    return key;
}
constexpr std::array<const char*, 4> reasons{"death", "administrative", "unloaded", "detached"};
void                                 validate(const SavedLifecycleInput& input) {
    const auto reason = int(input.reason);
    if (!input.subject.isValid() || input.sequence == 0 || input.source.owner.isNil() || input.source.epoch == 0 ||
        input.source.generation == 0 || reason < 0 || reason >= int(reasons.size()))
        invalid("Invalid durable lifecycle facts");
}
Value encode(const SavedLifecycleInput& input) {
    validate(input);
    return Value::object({{"subject", input.subject.format()},
                          {"sequence", std::to_string(input.sequence)},
                          {"reason", reasons[int(input.reason)]},
                          {"source", encodeSource(input.source)}});
}
SavedLifecycleInput decode(const Value& v) {
    const auto& o      = object(v, {"subject", "sequence", "reason", "source"});
    const auto& reason = text(o.at("reason"));
    std::size_t code   = 0;
    while (code < reasons.size() && reason != reasons[code]) ++code;
    if (code == reasons.size()) invalid("Unknown lifecycle reason");
    SavedLifecycleInput input{SubjectRef::fromPersistentId(identity(o.at("subject"))), counter(o.at("sequence")),
                              RemovalReason(code), decodeSource(o.at("source"))};
    validate(input);
    return input;
}
}  // namespace
Result<Value> encodeSavedSpawnerKey(const SavedSpawnerKey& key) {
    try {
        return Result<Value>::success(encodeSource(key));
    } catch (const Invalid& e) {
        return Result<Value>::failure(e.status);
    }
}
Result<SavedSpawnerKey> prepareSavedSpawnerKey(const Value& value) {
    try {
        return Result<SavedSpawnerKey>::success(decodeSource(value));
    } catch (const Invalid& e) {
        return Result<SavedSpawnerKey>::failure(e.status);
    }
}
Result<Value> encodeSavedLifecycleInputs(std::span<const SavedLifecycleInput> inputs) {
    try {
        if (inputs.size() > 1000000) invalid("Too many lifecycle inputs");
        Value::Array rows;
        rows.reserve(inputs.size());
        for (const auto& input : inputs) rows.push_back(encode(input));
        return Result<Value>::success(std::move(rows));
    } catch (const Invalid& error) {
        return Result<Value>::failure(error.status);
    }
}
Result<std::vector<SavedLifecycleInput>> prepareSavedLifecycleInputs(const Value& value) {
    using R = Result<std::vector<SavedLifecycleInput>>;
    try {
        const auto* a = value.getIf<Value::Array>();
        if (!a || a->size() > 1000000) invalid("Expected bounded lifecycle array");
        std::vector<SavedLifecycleInput> inputs;
        inputs.reserve(a->size());
        for (const auto& row : *a) inputs.push_back(decode(row));
        return R::success(std::move(inputs));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::spawn
