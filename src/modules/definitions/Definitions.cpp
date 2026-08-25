#include "definitions/Definitions.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::definitions {
namespace {

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                else
                    out << static_cast<char>(c);
        }
    }
    return out.str() + '"';
}

std::string canonicalJson(const eve::json::Value& value) {
    if (value.isNull()) return "null";
    if (value.isBool()) return value.asBool() ? "true" : "false";
    if (value.isNumber()) return value.asString();
    if (value.isString()) return quote(value.asString());
    if (value.isArray()) {
        std::string out = "[";
        for (size_t i = 0; i < value.size(); ++i) {
            if (i) out += ',';
            out += canonicalJson(value.at(i));
        }
        return out + ']';
    }
    if (value.isObject()) {
        auto keys = value.keys();
        std::sort(keys.begin(), keys.end());
        std::string out = "{";
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) out += ',';
            out += quote(keys[i]) + ':' + canonicalJson(value.get(keys[i].c_str()));
        }
        return out + '}';
    }
    return {};
}

bool parseU64(const eve::json::Value& value, uint64_t& result) {
    if (!value.isString()) return false;
    try {
        size_t used = 0;
        result      = std::stoull(value.asString(), &used);
        return used == value.asString().size();
    } catch (...) {
        return false;
    }
}

bool parsePositiveInt(const eve::json::Value& value, int& result) {
    if (!value.isNumber()) return false;
    const double number = value.asDouble();
    if (number <= 0 || number > std::numeric_limits<int>::max() || std::floor(number) != number) return false;
    result = static_cast<int>(number);
    return true;
}

}  // namespace

bool DefinitionRegistry::registerDefinition(const std::string& type, const std::string& id, int version,
                                            const std::string& json) {
    return mutate(false, type, id, version, json);
}

bool DefinitionRegistry::replaceDefinition(const std::string& type, const std::string& id, int version,
                                           const std::string& json) {
    return mutate(true, type, id, version, json);
}

bool DefinitionRegistry::mutate(bool replace, const std::string& type, const std::string& id, int version,
                                const std::string& json) {
    lastError_.clear();
    if (type.empty() || id.empty()) {
        lastError_ = "definition type and id must not be empty";
        return false;
    }
    if (version <= 0) {
        lastError_ = "definition version must be positive";
        return false;
    }
    const Key  key{type, id};
    const bool exists = definitions_.find(key) != definitions_.end();
    if (replace != exists) {
        lastError_ = replace ? "definition does not exist" : "definition already exists";
        return false;
    }
    std::string parseError;
    auto        document = eve::json::Document::parse(json, &parseError);
    if (!document.valid()) {
        lastError_ = parseError.empty() ? "definition must be valid JSON" : parseError;
        return false;
    }
    uint64_t& generation = generations_[key];
    if (generation == std::numeric_limits<uint64_t>::max() ||
        nextEventSequence_ == std::numeric_limits<uint64_t>::max()) {
        lastError_ = "definition generation or event sequence exhausted";
        return false;
    }
    ++generation;
    definitions_[key] = {type, id, version, generation, canonicalJson(document.root())};
    events_.push_back({nextEventSequence_++, "definition_reloaded", type, id, version, generation});
    view_.clear();
    return true;
}

bool DefinitionRegistry::remove(const std::string& type, const std::string& id) {
    lastError_.clear();
    const Key key{type, id};
    auto      it = definitions_.find(key);
    if (it == definitions_.end()) {
        lastError_ = "definition does not exist";
        return false;
    }
    uint64_t& generation = generations_[key];
    if (generation == std::numeric_limits<uint64_t>::max() ||
        nextEventSequence_ == std::numeric_limits<uint64_t>::max()) {
        lastError_ = "definition generation or event sequence exhausted";
        return false;
    }
    const int oldVersion = it->second.version;
    ++generation;
    definitions_.erase(it);
    events_.push_back({nextEventSequence_++, "definition_removed", type, id, oldVersion, generation});
    view_.clear();
    return true;
}

const Definition* DefinitionRegistry::resolve(const std::string& type, const std::string& id) const {
    const auto it = definitions_.find({type, id});
    return it == definitions_.end() ? nullptr : &it->second;
}

const Definition* DefinitionRegistry::resolveHandle(const DefinitionHandle& handle) const {
    return resolveGeneration(handle.type, handle.id, handle.generation);
}

const Definition* DefinitionRegistry::resolveGeneration(const std::string& type, const std::string& id,
                                                        uint64_t generation) const {
    const auto* definition = resolve(type, id);
    return definition && definition->generation == generation ? definition : nullptr;
}

const Definition* DefinitionRegistry::resolveRef(const DefinitionRef& reference) const {
    return resolve(reference.type, reference.id);
}

DefinitionHandle DefinitionRegistry::handle(const std::string& type, const std::string& id) const {
    const auto* definition = resolve(type, id);
    return definition ? DefinitionHandle{type, id, definition->generation} : DefinitionHandle{type, id, 0};
}

DefinitionRef DefinitionRegistry::reference(const std::string& type, const std::string& id) const { return {type, id}; }

int DefinitionRegistry::size() const { return static_cast<int>(definitions_.size()); }

int DefinitionRegistry::countType(const std::string& type) const {
    int result = 0;
    for (auto it = definitions_.lower_bound({type, {}}); it != definitions_.end() && it->first.first == type; ++it)
        ++result;
    return result;
}

void DefinitionRegistry::rebuildView() const {
    view_.clear();
    for (const auto& [key, definition] : definitions_)
        if (viewType_.empty() || key.first == viewType_) view_.push_back(&definition);
}

const Definition* DefinitionRegistry::at(int index) const {
    viewType_.clear();
    rebuildView();
    return index >= 0 && static_cast<size_t>(index) < view_.size() ? view_[static_cast<size_t>(index)] : nullptr;
}

const Definition* DefinitionRegistry::atType(const std::string& type, int index) const {
    viewType_ = type;
    rebuildView();
    return index >= 0 && static_cast<size_t>(index) < view_.size() ? view_[static_cast<size_t>(index)] : nullptr;
}

int DefinitionRegistry::eventCount() const { return static_cast<int>(events_.size()); }

const DefinitionEvent* DefinitionRegistry::eventAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}

void DefinitionRegistry::clearEvents() { events_.clear(); }

std::string DefinitionRegistry::snapshotJson() const {
    std::ostringstream out;
    out << "{\"definitions\":[";
    bool first = true;
    for (const auto& [key, definition] : definitions_) {
        if (!first) out << ',';
        first = false;
        out << "{\"generation\":" << quote(std::to_string(definition.generation)) << ",\"id\":" << quote(definition.id)
            << ",\"json\":" << definition.json << ",\"type\":" << quote(definition.type)
            << ",\"version\":" << definition.version << '}';
    }
    out << "],\"events\":[";
    first = true;
    for (const auto& event : events_) {
        if (!first) out << ',';
        first = false;
        out << "{\"generation\":" << quote(std::to_string(event.generation)) << ",\"id\":" << quote(event.id)
            << ",\"name\":" << quote(event.name) << ",\"sequence\":" << quote(std::to_string(event.sequence))
            << ",\"type\":" << quote(event.type) << ",\"version\":" << event.version << '}';
    }
    out << "],\"generations\":[";
    first = true;
    for (const auto& [key, generation] : generations_) {
        if (!first) out << ',';
        first = false;
        out << "{\"generation\":" << quote(std::to_string(generation)) << ",\"id\":" << quote(key.second)
            << ",\"type\":" << quote(key.first) << '}';
    }
    return out.str() + "],\"nextEventSequence\":" + quote(std::to_string(nextEventSequence_)) + ",\"version\":1}";
}

bool DefinitionRegistry::restoreJson(const std::string& json) {
    lastError_.clear();
    std::string error;
    auto        document = eve::json::Document::parse(json, &error);
    const auto  root     = document.root();
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1) {
        lastError_ = error.empty() ? "invalid definitions snapshot" : error;
        return false;
    }
    const auto definitions  = root.get("definitions");
    const auto generations  = root.get("generations");
    const auto events       = root.get("events");
    uint64_t   nextSequence = 0;
    if (!definitions.isArray() || !generations.isArray() || !events.isArray() ||
        !parseU64(root.get("nextEventSequence"), nextSequence) || nextSequence == 0) {
        lastError_ = "invalid definitions snapshot fields";
        return false;
    }

    std::map<Key, Definition>    restoredDefinitions;
    std::map<Key, uint64_t>      restoredGenerations;
    std::vector<DefinitionEvent> restoredEvents;
    for (size_t i = 0; i < generations.size(); ++i) {
        const auto value = generations.at(i);
        const Key  key{value.getString("type"), value.getString("id")};
        uint64_t   generation = 0;
        if (!value.isObject() || key.first.empty() || key.second.empty() ||
            !parseU64(value.get("generation"), generation) || generation == 0 ||
            !restoredGenerations.emplace(key, generation).second) {
            lastError_ = "invalid generation at index " + std::to_string(i);
            return false;
        }
    }
    for (size_t i = 0; i < definitions.size(); ++i) {
        const auto value = definitions.at(i);
        Definition definition;
        definition.type = value.getString("type");
        definition.id   = value.getString("id");
        if (!value.isObject() || definition.type.empty() || definition.id.empty() ||
            !parsePositiveInt(value.get("version"), definition.version) ||
            !parseU64(value.get("generation"), definition.generation) || definition.generation == 0 ||
            !value.has("json")) {
            lastError_ = "invalid definition at index " + std::to_string(i);
            return false;
        }
        const Key  key{definition.type, definition.id};
        const auto generation = restoredGenerations.find(key);
        if (generation == restoredGenerations.end() || generation->second != definition.generation ||
            !restoredDefinitions.emplace(key, definition).second) {
            lastError_ = "inconsistent definition generation at index " + std::to_string(i);
            return false;
        }
        restoredDefinitions[key].json = canonicalJson(value.get("json"));
    }
    uint64_t previousSequence = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto      value = events.at(i);
        DefinitionEvent event;
        event.name = value.getString("name");
        event.type = value.getString("type");
        event.id   = value.getString("id");
        if (!value.isObject() || (event.name != "definition_reloaded" && event.name != "definition_removed") ||
            event.type.empty() || event.id.empty() || !parsePositiveInt(value.get("version"), event.version) ||
            !parseU64(value.get("sequence"), event.sequence) || event.sequence <= previousSequence ||
            !parseU64(value.get("generation"), event.generation) || event.generation == 0) {
            lastError_ = "invalid event at index " + std::to_string(i);
            return false;
        }
        previousSequence = event.sequence;
        restoredEvents.push_back(std::move(event));
    }
    if (previousSequence >= nextSequence) {
        lastError_ = "next event sequence must exceed retained events";
        return false;
    }
    definitions_       = std::move(restoredDefinitions);
    generations_       = std::move(restoredGenerations);
    events_            = std::move(restoredEvents);
    nextEventSequence_ = nextSequence;
    view_.clear();
    viewType_.clear();
    return true;
}

const std::string& DefinitionRegistry::lastError() const { return lastError_; }

DefinitionRegistry* Definitions::newRegistry() {
    registries_.push_back(std::make_unique<DefinitionRegistry>());
    return registries_.back().get();
}

Module_IMPL(Definitions, new Definitions());

void Definitions::expose(ssq::Table& table) {
    auto definition = table.addClass<Definition>(
        "Definition", std::function<Definition*()>([]() -> Definition* { return nullptr; }), false);
    definition.addFunc("getType", [](Definition* value) { return value ? value->type : std::string{}; });
    definition.addFunc("getId", [](Definition* value) { return value ? value->id : std::string{}; });
    definition.addFunc("getVersion", [](Definition* value) { return value ? value->version : 0; });
    definition.addFunc("getGeneration",
                       [](Definition* value) { return value ? static_cast<int64_t>(value->generation) : int64_t{0}; });
    definition.addFunc("getJson", [](Definition* value) { return value ? value->json : std::string{}; });

    auto event = table.addClass<DefinitionEvent>(
        "DefinitionEvent", std::function<DefinitionEvent*()>([]() -> DefinitionEvent* { return nullptr; }), false);
    event.addFunc("getSequence",
                  [](DefinitionEvent* value) { return value ? static_cast<int64_t>(value->sequence) : int64_t{0}; });
    event.addFunc("getName", [](DefinitionEvent* value) { return value ? value->name : std::string{}; });
    event.addFunc("getType", [](DefinitionEvent* value) { return value ? value->type : std::string{}; });
    event.addFunc("getId", [](DefinitionEvent* value) { return value ? value->id : std::string{}; });
    event.addFunc("getVersion", [](DefinitionEvent* value) { return value ? value->version : 0; });
    event.addFunc("getGeneration",
                  [](DefinitionEvent* value) { return value ? static_cast<int64_t>(value->generation) : int64_t{0}; });

    auto registry = table.addClass<DefinitionRegistry>(
        "DefinitionRegistry", std::function<DefinitionRegistry*()>([]() -> DefinitionRegistry* { return nullptr; }),
        false);
    registry.addFunc("registerDefinition", &DefinitionRegistry::registerDefinition);
    registry.addFunc("replaceDefinition", &DefinitionRegistry::replaceDefinition);
    registry.addFunc("remove", &DefinitionRegistry::remove);
    registry.addFunc("resolve",
                     [](DefinitionRegistry* value, const std::string& type, const std::string& id) -> Definition* {
                         return value ? const_cast<Definition*>(value->resolve(type, id)) : nullptr;
                     });
    registry.addFunc("resolveGeneration",
                     [](DefinitionRegistry* value, const std::string& type, const std::string& id,
                        int64_t generation) -> Definition* {
                         return value && generation > 0 ? const_cast<Definition*>(value->resolveGeneration(
                                                              type, id, static_cast<uint64_t>(generation)))
                                                        : nullptr;
                     });
    registry.addFunc("size", &DefinitionRegistry::size);
    registry.addFunc("countType", &DefinitionRegistry::countType);
    registry.addFunc("at", [](DefinitionRegistry* value, int index) -> Definition* {
        return value ? const_cast<Definition*>(value->at(index)) : nullptr;
    });
    registry.addFunc("atType", [](DefinitionRegistry* value, const std::string& type, int index) -> Definition* {
        return value ? const_cast<Definition*>(value->atType(type, index)) : nullptr;
    });
    registry.addFunc("eventCount", &DefinitionRegistry::eventCount);
    registry.addFunc("eventAt", [](DefinitionRegistry* value, int index) -> DefinitionEvent* {
        return value ? const_cast<DefinitionEvent*>(value->eventAt(index)) : nullptr;
    });
    registry.addFunc("clearEvents", &DefinitionRegistry::clearEvents);
    registry.addFunc("snapshotJson", &DefinitionRegistry::snapshotJson);
    registry.addFunc("restoreJson", &DefinitionRegistry::restoreJson);
    registry.addFunc("lastError", &DefinitionRegistry::lastError);

    auto cls = table.addClass(name, Definitions::create, false);
    expose(cls);
}

void Definitions::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Definitions::getName);
    cls.addFunc("newRegistry", &Definitions::newRegistry);
}

}  // namespace eve::definitions
