/** @file EffectContainerState.cpp
 * @brief effects module-interface: complete effects.container v1 data face, without ECS/services or callback replay.
 * @remarks Mutable lifecycle state remains owned by EffectContainer; wire values are boundary transfers only.
 */
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <type_traits>
#include "effects/EffectContainer.h"
namespace eve::effects {
namespace {
static_assert(int(StackMode::Replace) == 0 && int(StackMode::NewInstance) == 1 && int(StackMode::Reuse) == 2 &&
              int(StackMode::Accumulate) == 3);
static_assert(int(StackCountPolicy::Keep) == 0 && int(StackCountPolicy::Increment) == 1 &&
              int(StackCountPolicy::Set) == 2);
static_assert(int(DurationPolicy::Keep) == 0 && int(DurationPolicy::Replace) == 1 && int(DurationPolicy::Extend) == 2);
static_assert(int(MagnitudePolicy::Keep) == 0 && int(MagnitudePolicy::Replace) == 1 && int(MagnitudePolicy::Add) == 2 &&
              int(MagnitudePolicy::Max) == 3);
static_assert(int(OverflowPolicy::Reject) == 0 && int(OverflowPolicy::Clamp) == 1 &&
              int(OverflowPolicy::ReplaceOldest) == 2);
static_assert(int(EffectEventKind::Applied) == 0 && int(EffectEventKind::Refreshed) == 1 &&
              int(EffectEventKind::Stacked) == 2 && int(EffectEventKind::Periodic) == 3 &&
              int(EffectEventKind::Expired) == 4 && int(EffectEventKind::Removed) == 5);
static_assert(int(definition::ReloadPolicy::KeepInstanceValues) == 0 &&
              int(definition::ReloadPolicy::ReapplyDefaults) == 1 &&
              int(definition::ReloadPolicy::RebuildInstance) == 2 &&
              int(definition::ReloadPolicy::RejectWhileActive) == 3);
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "effects.container"))};
}
template <class T>
T take(Result<T> result) {
    if (!result) throw Invalid{result.status()};
    return std::move(result).takeValue();
}
const Value::Array& array(const Value& v) {
    const auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > 1048576) invalid("Invalid effects array");
    return *a;
}
const std::string& text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected effect text");
    return *s;
}
std::uint64_t counter(const Value& v) {
    const auto&   s = text(v);
    std::uint64_t n = 0;
    const auto    p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s)
        invalid("Invalid effect counter");
    return n;
}
PersistentId uuid(const Value& v) {
    const auto& s  = text(v);
    auto        id = PersistentId::parse(s);
    if (!id || id->format() != s) invalid("Invalid effect UUID");
    return *id;
}
template <class T>
auto fields(T& v) {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, EffectPolicy>)
        return std::tie(v.stackMode, v.stackCount, v.duration, v.magnitude, v.overflow, v.maxStacks);
    else if constexpr (std::is_same_v<U, EffectInstance>)
        return std::tie(v.id, v.subject, v.type, v.source, v.stackKey, v.priority, v.duration, v.remaining, v.period,
                        v.periodElapsed, v.magnitude, v.stackCount, v.payload, v.tags, v.policy, v.identity,
                        v.definitionIdentity, v.reloadPolicy);
    else if constexpr (std::is_same_v<U, EffectEvent>)
        return std::tie(v.sequence, v.kind, v.effectId, v.subject, v.type, v.source, v.reason, v.effectIdentity,
                        v.tick);
}
template <class T>
constexpr int maximumEnum() {
    if constexpr (std::is_same_v<T, StackMode> || std::is_same_v<T, MagnitudePolicy> ||
                  std::is_same_v<T, definition::ReloadPolicy>)
        return 3;
    else if constexpr (std::is_same_v<T, StackCountPolicy> || std::is_same_v<T, DurationPolicy> ||
                       std::is_same_v<T, OverflowPolicy>)
        return 2;
    else if constexpr (std::is_same_v<T, EffectEventKind>)
        return 5;
    else
        static_assert(!sizeof(T), "Undeclared effect wire enum");
}
template <class T>
Value encode(const T& v) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool> || std::is_same_v<T, int>)
        return Value(v);
    else if constexpr (std::is_same_v<T, double>) {
        if (!std::isfinite(v)) invalid("Nonfinite effect number");
        return Value(v);
    } else if constexpr (std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint64_t>)
        return std::to_string(v);
    else if constexpr (std::is_enum_v<T>) {
        if (int(v) < 0 || int(v) > maximumEnum<T>()) invalid("Unknown effect enum");
        return int(v);
    } else if constexpr (std::is_same_v<T, EffectId>)
        return v.format();
    else if constexpr (std::is_same_v<T, SimulationTick>)
        return std::to_string(v.value());
    else if constexpr (std::is_same_v<T, EffectPayload>) {
        Value result(v.object());
        auto  json = result.toJson();
        if (!json) throw Invalid{json.status()};
        return result;
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        Value::Array a;
        for (const auto& s : v) a.emplace_back(s);
        return a;
    } else if constexpr (std::is_same_v<T, definition::InstanceIdentity>) {
        if (v.instanceId.isNil() && !v.definition.id().isValid() && v.definitionGeneration.isZero()) return Value{};
        return Value::Array{v.instanceId.format(), v.definition.id().format(),
                            std::to_string(v.definitionGeneration.value())};
    } else {
        Value::Array a;
        std::apply([&](const auto&... x) { (a.push_back(encode(x)), ...); }, fields(v));
        return a;
    }
}
template <class T>
void decode(const Value& v, T& out) {
    if constexpr (std::is_same_v<T, std::string>)
        out = text(v);
    else if constexpr (std::is_same_v<T, bool>) {
        const auto* b = v.getIf<bool>();
        if (!b) invalid("Expected effect boolean");
        out = *b;
    } else if constexpr (std::is_same_v<T, int>) {
        if (!v.isInt64() || v.asInt() < std::numeric_limits<int>::min() || v.asInt() > std::numeric_limits<int>::max())
            invalid("Effect integer exceeds range");
        out = int(v.asInt());
    } else if constexpr (std::is_same_v<T, double>) {
        if (!v.isDouble() && !v.isInt64()) invalid("Expected effect number");
        out = v.isDouble() ? v.asDouble() : double(v.asInt());
        if (!std::isfinite(out)) invalid("Nonfinite effect number");
    } else if constexpr (std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint64_t>) {
        auto n = counter(v);
        if (n > std::numeric_limits<T>::max()) invalid("Effect counter exceeds range");
        out = T(n);
    } else if constexpr (std::is_enum_v<T>) {
        if (!v.isInt64() || v.asInt() < 0 || v.asInt() > maximumEnum<T>()) invalid("Unknown effect enum");
        out = T(v.asInt());
    } else if constexpr (std::is_same_v<T, EffectId>)
        out = EffectId::fromUuid(uuid(v));
    else if constexpr (std::is_same_v<T, SimulationTick>)
        out = SimulationTick(counter(v));
    else if constexpr (std::is_same_v<T, EffectPayload>) {
        const auto* object = v.getIf<Value::Object>();
        if (!object) invalid("Expected effect payload object");
        for (const auto& [key, item] : *object) {
            if (key.empty()) invalid("Empty effect payload key");
            auto json   = take(item.toJson());
            auto stored = out.setJson(key, json);
            if (!stored) throw Invalid{stored.status()};
        }
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        for (const auto& item : array(v)) out.push_back(text(item));
    } else if constexpr (std::is_same_v<T, definition::InstanceIdentity>) {
        if (v.isNull()) {
            out = {};
            return;
        }
        const auto& a = array(v);
        if (a.size() != 3) invalid("Unknown effect definition identity fields");
        auto id  = uuid(a[0]);
        auto ref = take(DefinitionRef::parse(text(a[1])));
        if (ref.id().format() != text(a[1])) invalid("Noncanonical effect definition reference");
        out = take(definition::InstanceIdentity::create(id, std::move(ref), Generation(counter(a[2]))));
    } else {
        auto        f = fields(out);
        const auto& a = array(v);
        if (a.size() != std::tuple_size_v<decltype(f)>) invalid("Unknown or missing effect tuple fields");
        std::size_t i = 0;
        std::apply([&](auto&... x) { (decode(a[i++], x), ...); }, f);
    }
}
void validateIdentity(const std::string& id, EffectId identity, std::uint64_t nextId) {
    if (!identity.isNil()) {
        if (id != identity.format()) invalid("Effect UUID projection differs");
        return;
    }
    constexpr std::string_view prefix = "effect-";
    if (!id.starts_with(prefix)) invalid("Invalid legacy effect id");
    const auto    digits = std::string_view(id).substr(prefix.size());
    std::uint64_t n      = 0;
    const auto    parsed = std::from_chars(digits.data(), digits.data() + digits.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || !n || n >= nextId)
        invalid("Invalid effect id watermark");
    auto spelling = std::to_string(n);
    if (spelling.size() < 16) spelling.insert(0, 16 - spelling.size(), '0');
    if (spelling != digits) invalid("Noncanonical legacy effect id");
}
}  // namespace
Result<void> EffectContainer::validateState() const {
    try {
        if (!nextId_ || !nextSequence_ || effects_.size() > 1048576 || (events_ && events_->size() > 1048576) ||
            (!hasLastTick_ && lastTick_.value()))
            invalid("Invalid effect counters or clock");
        std::set<std::string> ids;
        for (const auto& pointer : effects_) {
            if (!pointer) invalid("Null effect instance");
            const auto& e = *pointer;
            if (!ids.insert(e.id).second || e.subject.empty() || e.type.empty() || !std::isfinite(e.duration) ||
                !std::isfinite(e.remaining) || !std::isfinite(e.period) || !std::isfinite(e.periodElapsed) ||
                !std::isfinite(e.magnitude) || e.period < 0 || e.periodElapsed < 0 ||
                (e.period > 0 && e.periodElapsed >= e.period) || (e.period == 0 && e.periodElapsed != 0) ||
                (e.remaining <= 0 && e.remaining != -1) || !e.stackCount || e.tags.size() > 1048576 ||
                !std::is_sorted(e.tags.begin(), e.tags.end()) ||
                std::adjacent_find(e.tags.begin(), e.tags.end()) != e.tags.end())
                invalid("Invalid effect lifecycle facts");
            validateIdentity(e.id, e.identity, nextId_);
            const auto& d = e.definitionIdentity;
            if (!d.instanceId.isNil() || d.definition.id().isValid() || !d.definitionGeneration.isZero()) {
                auto checked = definition::InstanceIdentity::create(d.instanceId, d.definition, d.definitionGeneration);
                if (!checked) throw Invalid{checked.status()};
                if (e.identity.isNil() || e.identity.format() != d.instanceId.format())
                    invalid("Effect definition instance differs");
            }
            // The same typed writer checks all enum dimensions and recursive payload values.
            (void)encode(e);
        }
        std::uint64_t previous = 0;
        if (events_) {
            for (const auto& e : *events_) {
                if (!e.sequence || e.sequence <= previous || e.sequence >= nextSequence_ || e.subject.empty() ||
                    e.type.empty() || e.tick > lastTick_)
                    invalid("Invalid effect event history");
                validateIdentity(e.effectId, e.effectIdentity, nextId_);
                previous = e.sequence;
                (void)encode(e.kind);
            }
        }
        return Result<void>::success();
    } catch (const Invalid& e) {
        return Result<void>::failure(e.status);
    }
}
Result<Value> EffectContainer::encodeState() const {
    if (effectIdGenerator_)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Effect UUID callback implementation has no persistent identity protocol",
            "effects.container.generator"));
    auto valid = validateState();
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Array instances, events;
    for (const auto& e : effects_) instances.push_back(encode(*e));
    if (events_)
        for (const auto& e : *events_) events.push_back(encode(e));
    return Result<Value>::success(Value::object({{"schema", "effects.container"},
                                                 {"version", 1},
                                                 {"nextId", std::to_string(nextId_)},
                                                 {"nextSequence", std::to_string(nextSequence_)},
                                                 {"tick", std::to_string(lastTick_.value())},
                                                 {"hasTick", hasLastTick_},
                                                 {"instances", std::move(instances)},
                                                 {"events", std::move(events)}}));
}
Result<EffectContainer> EffectContainer::prepareState(const Value& value) {
    try {
        const auto* o = value.getIf<Value::Object>();
        if (!o || o->size() != 8) invalid("Unknown or missing effect container fields");
        for (const auto* key :
             {"schema", "version", "nextId", "nextSequence", "tick", "hasTick", "instances", "events"})
            if (!o->contains(key)) invalid("Missing effect container field");
        if (text(o->at("schema")) != "effects.container" || !o->at("version").isInt64() ||
            o->at("version").asInt() != 1)
            invalid("Unsupported effect container schema or version");
        EffectContainer result;
        result.nextId_       = counter(o->at("nextId"));
        result.nextSequence_ = counter(o->at("nextSequence"));
        result.lastTick_     = SimulationTick(counter(o->at("tick")));
        decode(o->at("hasTick"), result.hasLastTick_);
        for (const auto& row : array(o->at("instances"))) {
            auto instance = std::make_unique<EffectInstance>();
            decode(row, *instance);
            result.effects_.push_back(std::move(instance));
        }
        const auto& retainedEvents = array(o->at("events"));
        if (!retainedEvents.empty()) result.events_ = std::make_unique<std::deque<EffectEvent>>();
        for (const auto& row : retainedEvents) {
            EffectEvent event;
            decode(row, event);
            result.events_->push_back(std::move(event));
        }
        auto valid = result.validateState();
        if (!valid) return Result<EffectContainer>::failure(valid.status());
        return Result<EffectContainer>::success(std::move(result));
    } catch (const Invalid& e) {
        return Result<EffectContainer>::failure(e.status);
    }
}
}  // namespace eve::effects
