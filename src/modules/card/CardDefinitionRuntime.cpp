#include "card/CardDefinitionRuntime.h"

#include "common/Value.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace eve::card {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
        "card.definition_runtime"));
}

eve::Result<eve::definition::DefinitionHandle> currentHandle(
    eve::definitions::DefinitionRegistry& registry, const eve::DefinitionRef& reference) {
    const auto& logical = reference.id();
    return registry.handle(std::string(logical.namespaceName()), std::string(logical.name()));
}

const eve::Value* field(const eve::Value::Object& object, const char* name) {
    const auto it = object.find(name);
    return it == object.end() ? nullptr : &it->second;
}

bool readInt(const eve::Value::Object& object, const char* name, int& output) {
    const auto* value = field(object, name);
    if (const auto* integer = value ? value->getIf<std::int64_t>() : nullptr) {
        if (*integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max())
            return false;
        output = static_cast<int>(*integer);
        return true;
    }
    return false;
}

eve::Result<CardRuntimeState> parseState(const eve::definitions::Definition& definition,
                                         const eve::DefinitionRef& reference) {
    auto parsed = eve::Value::fromJson(definition.json);
    if (!parsed) return eve::Result<CardRuntimeState>::failure(parsed.status());
    auto value = std::move(parsed).takeValue();
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return invalid<CardRuntimeState>("card definition payload must be an object", "json");

    CardRuntimeState state;
    state.name = std::string(reference.id().name());
    if (const auto* value = field(*object, "name")) {
        const auto* text = value->getIf<std::string>();
        if (!text) return invalid<CardRuntimeState>("card name must be a string", "name");
        state.name = *text;
    }
    if (const auto* value = field(*object, "kind")) {
        const auto* text = value->getIf<std::string>();
        if (!text) return invalid<CardRuntimeState>("card kind must be a string", "kind");
        state.kind = *text;
    }
    if (!readInt(*object, "cost", state.cost) || !readInt(*object, "attack", state.attack) ||
        !readInt(*object, "health", state.maxHealth))
        return invalid<CardRuntimeState>("card cost, attack and health must be Int64 fields", "stats");
    if (state.cost < 0 || state.attack < 0 || state.maxHealth < 0)
        return invalid<CardRuntimeState>("card stats must not be negative", "stats");
    state.currentHealth = state.maxHealth;

    if (const auto* value = field(*object, "tint")) {
        const auto* array = value->getIf<eve::Value::Array>();
        if (!array || array->size() < 3)
            return invalid<CardRuntimeState>("card tint must contain three numeric channels", "tint");
        float* channels[] = {&state.tint.r, &state.tint.g, &state.tint.b};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto* number = (*array)[i].getIf<double>();
            const auto* integer = (*array)[i].getIf<std::int64_t>();
            const double channel = number ? *number : integer ? static_cast<double>(*integer) : -1.0;
            if (!std::isfinite(channel) || channel < 0.0 || channel > 1.0)
                return invalid<CardRuntimeState>("card tint channel must be finite in [0,1]", "tint");
            *channels[i] = static_cast<float>(channel);
        }
    }
    if (const auto* value = field(*object, "tags")) {
        const auto* array = value->getIf<eve::Value::Array>();
        if (!array) return invalid<CardRuntimeState>("card tags must be an array", "tags");
        for (const auto& item : *array) {
            const auto* tag = item.getIf<std::string>();
            if (!tag || tag->empty())
                return invalid<CardRuntimeState>("card tag must be a non-empty string", "tags");
            state.tags.push_back(*tag);
        }
        std::sort(state.tags.begin(), state.tags.end());
        state.tags.erase(std::unique(state.tags.begin(), state.tags.end()), state.tags.end());
    }
    return eve::Result<CardRuntimeState>::success(std::move(state));
}

eve::Result<CardRuntimeState> resolveState(eve::definitions::DefinitionRegistry& registry,
                                           const eve::DefinitionRef& reference,
                                           const eve::definition::DefinitionHandle& handle) {
    auto definition = registry.resolveHandle(handle);
    if (!definition) return eve::Result<CardRuntimeState>::failure(definition.status());
    return parseState(definition.value().get(), reference);
}

eve::LogicalId cardSchema() {
    static const eve::LogicalId value = [] {
        const auto parsed = eve::LogicalId::parse("card:runtime");
        if (!parsed) throw std::logic_error("invalid built-in card snapshot schema");
        return *parsed;
    }();
    return value;
}

eve::Value encodeState(const CardRuntimeState& state) {
    eve::Value::Array tint{eve::Value(static_cast<double>(state.tint.r)),
                           eve::Value(static_cast<double>(state.tint.g)),
                           eve::Value(static_cast<double>(state.tint.b))};
    eve::Value::Array tags;
    for (const auto& tag : state.tags) tags.emplace_back(tag);
    return eve::Value(eve::Value::Object{
        {"attack", eve::Value(static_cast<std::int64_t>(state.attack))},
        {"cost", eve::Value(static_cast<std::int64_t>(state.cost))},
        {"currentHealth", eve::Value(static_cast<std::int64_t>(state.currentHealth))},
        {"kind", eve::Value(state.kind)}, {"maxHealth", eve::Value(static_cast<std::int64_t>(state.maxHealth))},
        {"name", eve::Value(state.name)}, {"tags", eve::Value(std::move(tags))},
        {"tint", eve::Value(std::move(tint))},
    });
}

eve::Result<CardRuntimeState> decodeState(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr) return invalid<CardRuntimeState>("card runtime state must be an object", "state");
    static const std::set<std::string> fields = {"attack", "cost", "currentHealth", "kind", "maxHealth", "name", "tags", "tint"};
    for (const auto& [name, unused] : *object) {
        (void)unused;
        if (!fields.contains(name)) return invalid<CardRuntimeState>("unknown card runtime state field", "state." + name);
    }
    for (const auto& name : fields)
        if (!object->contains(name)) return invalid<CardRuntimeState>("card runtime state is missing a field", "state." + name);
    CardRuntimeState result;
    const auto readIntField = [&](const char* name, int& output) {
        const auto* integer = object->at(name).getIf<std::int64_t>();
        if (integer == nullptr || *integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max()) return false;
        output = static_cast<int>(*integer);
        return true;
    };
    const auto* name = object->at("name").getIf<std::string>();
    const auto* kind = object->at("kind").getIf<std::string>();
    const auto* tags = object->at("tags").getIf<eve::Value::Array>();
    const auto* tint = object->at("tint").getIf<eve::Value::Array>();
    if (name == nullptr || kind == nullptr || tags == nullptr || tint == nullptr || tint->size() != 3 ||
        !readIntField("attack", result.attack) || !readIntField("cost", result.cost) ||
        !readIntField("currentHealth", result.currentHealth) || !readIntField("maxHealth", result.maxHealth))
        return invalid<CardRuntimeState>("card runtime state field is invalid", "state");
    result.name = *name;
    result.kind = *kind;
    if (result.cost < 0 || result.attack < 0 || result.maxHealth < 0 || result.currentHealth < 0 || result.currentHealth > result.maxHealth)
        return invalid<CardRuntimeState>("card runtime health/stat values are invalid", "state");
    float* channels[] = {&result.tint.r, &result.tint.g, &result.tint.b};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto* number = (*tint)[index].getIf<double>();
        if (number == nullptr || !std::isfinite(*number) || *number < 0.0 || *number > 1.0) return invalid<CardRuntimeState>("card tint channel is invalid", "state.tint");
        *channels[index] = static_cast<float>(*number);
    }
    for (const auto& tag : *tags) {
        const auto* text = tag.getIf<std::string>();
        if (text == nullptr || text->empty()) return invalid<CardRuntimeState>("card runtime tag is invalid", "state.tags");
        result.tags.push_back(*text);
    }
    return eve::Result<CardRuntimeState>::success(std::move(result));
}

}  // namespace

eve::Result<CardDefinitionRuntime> CardDefinitionRuntime::create(
    eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition,
    eve::PersistentId instanceId, eve::definition::ReloadPolicy policy) {
    if (!definition.id().isValid()) return invalid<CardDefinitionRuntime>("card definition reference is invalid");
    auto handle = currentHandle(registry, definition);
    if (!handle) return eve::Result<CardDefinitionRuntime>::failure(handle.status());
    auto state = resolveState(registry, definition, handle.value());
    if (!state) return eve::Result<CardDefinitionRuntime>::failure(state.status());
    auto runtime = eve::definition::RuntimeInstance<CardRuntimeState>::create(
        instanceId, std::move(definition), handle.value().generation, std::move(state).takeValue());
    if (!runtime) return eve::Result<CardDefinitionRuntime>::failure(runtime.status());
    return eve::Result<CardDefinitionRuntime>::success(
        CardDefinitionRuntime(registry, std::move(runtime).takeValue(), policy));
}

const eve::definition::InstanceIdentity& CardDefinitionRuntime::identity() const noexcept {
    return runtime_.identity();
}

const CardRuntimeState& CardDefinitionRuntime::state() const noexcept { return runtime_.state(); }

CardRuntimeState& CardDefinitionRuntime::state() noexcept { return runtime_.state(); }

eve::definition::DefinitionHandle CardDefinitionRuntime::definitionHandle() const noexcept {
    return runtime_.identity().definitionHandle();
}

void CardDefinitionRuntime::setActive(bool active) noexcept { runtime_.setActive(active); }

bool CardDefinitionRuntime::isActive() const noexcept { return runtime_.isActive(); }

eve::Result<void> CardDefinitionRuntime::applyTo(CardData* card) const {
    if (card == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "card runtime cannot project to a null CardData", "card",
            {}, "card.definition_runtime"));

    CardData::Identity identity = *card->identity().operator->();
    CardData::Stats stats = *card->stats().operator->();
    CardData::Visual visual = *card->visual().operator->();
    CardData::DefinitionBinding binding = *card->definitionBinding().operator->();

    identity.definitionId = this->identity().definition.id().name();
    identity.name = state().name;
    identity.kind = state().kind;
    stats.cost = state().cost;
    stats.attack = state().attack;
    stats.health = state().currentHealth;
    visual.tint = state().tint;
    binding.identity = this->identity();
    binding.reloadPolicy = policy_;
    binding.active = isActive();

    using std::swap;
    swap(*card->identity().operator->(), identity);
    swap(*card->stats().operator->(), stats);
    swap(*card->visual().operator->(), visual);
    swap(*card->definitionBinding().operator->(), binding);
    return eve::Result<void>::success();
}

eve::Result<eve::definition::ReloadOutcome> CardDefinitionRuntime::reload(
    eve::definition::ReloadPolicy policy) {
    if (registry_ == nullptr)
        return invalid<eve::definition::ReloadOutcome>("card definition registry is not bound", "registry");
    auto next = currentHandle(*registry_, identity().definition);
    if (!next) return eve::Result<eve::definition::ReloadOutcome>::failure(next.status());
    auto defaults = resolveState(*registry_, identity().definition, next.value());
    if (!defaults) return eve::Result<eve::definition::ReloadOutcome>::failure(defaults.status());
    CardRuntimeState defaultState = std::move(defaults).takeValue();
    auto rebuild = [defaultState](const CardRuntimeState& oldState,
                                  const eve::definition::InstanceIdentity&,
                                  const eve::definition::DefinitionHandle&) mutable
        -> eve::Result<CardRuntimeState> {
        CardRuntimeState rebuilt = defaultState;
        rebuilt.currentHealth = std::clamp(oldState.currentHealth, 0, rebuilt.maxHealth);
        return eve::Result<CardRuntimeState>::success(std::move(rebuilt));
    };
    auto result = runtime_.reload(next.value(), policy, defaultState, std::move(rebuild));
    if (result.ok()) policy_ = policy;
    return result;
}

eve::Result<eve::SnapshotEnvelope> CardDefinitionRuntime::snapshot(
    eve::Revision revision, eve::SimulationTick tick,
    const eve::SnapshotHashProvider& hashProvider) const {
    const eve::definition::RuntimeStateEncoder<CardRuntimeState> encoder =
        [](const CardRuntimeState& value) { return eve::Result<eve::Value>::success(encodeState(value)); };
    return eve::definition::snapshotRuntimeInstance(
        runtime_, "card.runtime", cardSchema(), eve::SchemaVersion(1), revision, tick,
        hashProvider, encoder);
}

eve::Result<std::string> CardDefinitionRuntime::snapshotJson(
    eve::Revision revision, eve::SimulationTick tick,
    const eve::SnapshotHashProvider& hashProvider) const {
    auto result = snapshot(revision, tick, hashProvider);
    if (!result) return eve::Result<std::string>::failure(result.status());
    return std::move(result).andThen(
        [](eve::SnapshotEnvelope&& value) { return eve::serializeSnapshotEnvelope(value); });
}

eve::Result<void> CardDefinitionRuntime::restore(
    const eve::SnapshotEnvelope& snapshotValue, const eve::SnapshotHashProvider& hashProvider) {
    if (registry_ == nullptr) return invalid<void>("card definition registry is not bound", "registry");
    auto current = currentHandle(*registry_, identity().definition);
    if (!current) return eve::Result<void>::failure(current.status());
    if (current.value() != definitionHandle())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "card snapshot cannot restore against a replaced definition",
            "definitionGeneration", {}, "card.definition_runtime"));
    const eve::definition::RuntimeStateDecoder<CardRuntimeState> decoder = decodeState;
    return eve::definition::restoreRuntimeInstance(
        runtime_, snapshotValue, "card.runtime", cardSchema(), eve::SchemaVersion(1),
        hashProvider, decoder);
}

}  // namespace eve::card
