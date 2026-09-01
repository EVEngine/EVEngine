#include "tags/GameplayTag.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace eve::tags {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

std::string idText(GameplayTagId id) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << id.value();
    return stream.str();
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

const Value* member(const Value& value, std::string_view key) { return value.find(std::string(key)); }

}  // namespace

bool isValidGameplayTagName(std::string_view name) noexcept {
    if (name.empty() || name.front() == '.' || name.back() == '.') return false;
    bool segmentHasCharacter = false;
    for (const unsigned char c : name) {
        if (c == '.') {
            if (!segmentHasCharacter) return false;
            segmentHasCharacter = false;
            continue;
        }
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
        segmentHasCharacter = true;
    }
    return segmentHasCharacter;
}

GameplayTagId gameplayTagId(std::string_view name) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return GameplayTagId(hash);
}

bool gameplayTagMatches(std::string_view candidate, std::string_view query, GameplayTagMatch match) noexcept {
    if (candidate == query) return true;
    return match == GameplayTagMatch::IncludeDescendants && candidate.size() > query.size() &&
           candidate.starts_with(query) && candidate[query.size()] == '.';
}

Result<GameplayTagId> GameplayTagRegistry::registerTag(std::string name, std::string description) {
    if (!isValidGameplayTagName(name)) return invalid<GameplayTagId>("Invalid gameplay-tag name", "name");
    const GameplayTagId id = gameplayTagId(name);
    if (const auto existing = definitionsByName_.find(name); existing != definitionsByName_.end()) {
        if (existing->second.description != description)
            return Result<GameplayTagId>::failure(Diagnostic::error(
                DiagnosticCode::AlreadyExists, "Gameplay tag is already registered with different metadata", name));
        return Result<GameplayTagId>::success(id, Status::success(StatusCode::NoOp));
    }
    if (const auto collision = nameById_.find(id); collision != nameById_.end() && collision->second != name)
        return Result<GameplayTagId>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Gameplay-tag hash collision", name));
    definitionsByName_.emplace(name, GameplayTagDefinition{id, name, std::move(description)});
    nameById_.emplace(id, std::move(name));
    return Result<GameplayTagId>::success(id, Status::success(StatusCode::Applied));
}

Result<GameplayTagDefinition> GameplayTagRegistry::find(std::string_view name) const {
    const auto found = definitionsByName_.find(name);
    if (found == definitionsByName_.end())
        return Result<GameplayTagDefinition>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Gameplay tag is not registered", std::string(name)));
    return Result<GameplayTagDefinition>::success(found->second);
}

bool GameplayTagRegistry::contains(std::string_view name) const { return definitionsByName_.contains(name); }

std::vector<GameplayTagDefinition> GameplayTagRegistry::definitions() const {
    std::vector<GameplayTagDefinition> result;
    result.reserve(definitionsByName_.size());
    for (const auto& [name, definition] : definitionsByName_) result.push_back(definition);
    return result;
}

std::vector<GameplayTagDefinition> GameplayTagRegistry::search(std::string_view text, std::string_view root) const {
    const std::string                  needle = lower(text);
    std::vector<GameplayTagDefinition> result;
    for (const auto& [name, definition] : definitionsByName_) {
        if (!root.empty() && !gameplayTagMatches(name, root, GameplayTagMatch::IncludeDescendants)) continue;
        if (!needle.empty() && lower(name + " " + definition.description).find(needle) == std::string::npos) continue;
        result.push_back(definition);
    }
    return result;
}

Result<Value> GameplayTagRegistry::toValue() const {
    Value::Array tags;
    tags.reserve(definitionsByName_.size());
    for (const auto& [name, definition] : definitionsByName_) {
        tags.emplace_back(
            Value::Object{{"description", definition.description}, {"id", idText(definition.id)}, {"name", name}});
    }
    return Result<Value>::success(
        Value(Value::Object{{"schema", "eve.gameplay-tags"}, {"tags", std::move(tags)}, {"version", 1}}));
}

Result<GameplayTagRegistry> GameplayTagRegistry::fromValue(const Value& value) {
    if (!value.isObject()) return invalid<GameplayTagRegistry>("Gameplay-tag registry must be an object", "$");
    const Value* schema  = member(value, "schema");
    const Value* version = member(value, "version");
    const Value* tags    = member(value, "tags");
    if (!schema || !schema->isString() || schema->asString() != "eve.gameplay-tags")
        return invalid<GameplayTagRegistry>("Unsupported gameplay-tag schema", "schema");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return invalid<GameplayTagRegistry>("Unsupported gameplay-tag schema version", "version");
    if (!tags || !tags->isArray()) return invalid<GameplayTagRegistry>("Gameplay tags must be an array", "tags");

    GameplayTagRegistry candidate;
    for (std::size_t i = 0; i < tags->arraySize(); ++i) {
        const auto&       entry = tags->at(i);
        const std::string path  = "tags[" + std::to_string(i) + "]";
        if (!entry.isObject()) return invalid<GameplayTagRegistry>("Gameplay-tag entry must be an object", path);
        const Value* name        = member(entry, "name");
        const Value* description = member(entry, "description");
        const Value* id          = member(entry, "id");
        if (!name || !name->isString())
            return invalid<GameplayTagRegistry>("Gameplay-tag name must be a string", path + ".name");
        if (!description || !description->isString())
            return invalid<GameplayTagRegistry>("Gameplay-tag description must be a string", path + ".description");
        if (!id || !id->isString() || id->asString() != idText(gameplayTagId(name->asString())))
            return invalid<GameplayTagRegistry>("Gameplay-tag id does not match its canonical name", path + ".id");
        auto registered = candidate.registerTag(name->asString(), description->asString());
        if (!registered) return Result<GameplayTagRegistry>::failure(registered.status());
    }
    return Result<GameplayTagRegistry>::success(std::move(candidate));
}

}  // namespace eve::tags
