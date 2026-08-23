#include "policyregistry/PolicyRegistry.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace eve::policyregistry {
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
    auto keys = value.keys();
    std::sort(keys.begin(), keys.end());
    std::string out = "{";
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i) out += ',';
        out += quote(keys[i]) + ':' + canonicalJson(value.get(keys[i].c_str()));
    }
    return out + '}';
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

bool parseInt(const eve::json::Value& value, int& result, bool positive) {
    if (!value.isNumber()) return false;
    const double number = value.asDouble();
    if ((positive && number <= 0) || number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max() || std::floor(number) != number)
        return false;
    result = static_cast<int>(number);
    return true;
}

}  // namespace

std::string implementationKindName(ImplementationKind kind) {
    switch (kind) {
        case ImplementationKind::Builtin: return "builtin";
        case ImplementationKind::Script: return "script";
        case ImplementationKind::Batch: return "batch";
    }
    return {};
}

bool parseImplementationKind(const std::string& name, ImplementationKind& kind) {
    if (name == "builtin")
        kind = ImplementationKind::Builtin;
    else if (name == "script")
        kind = ImplementationKind::Script;
    else if (name == "batch")
        kind = ImplementationKind::Batch;
    else
        return false;
    return true;
}

bool PolicyRegistry::registerPolicy(const std::string& domain, const std::string& name, int version, int priority,
                                    bool enabled, const std::string& kind, const std::string& schemaId,
                                    const std::string& metadataJson) {
    return mutate(false, domain, name, version, priority, enabled, kind, schemaId, metadataJson);
}

bool PolicyRegistry::replacePolicy(const std::string& domain, const std::string& name, int version, int priority,
                                   bool enabled, const std::string& kind, const std::string& schemaId,
                                   const std::string& metadataJson) {
    return mutate(true, domain, name, version, priority, enabled, kind, schemaId, metadataJson);
}

bool PolicyRegistry::mutate(bool replace, const std::string& domain, const std::string& name, int version, int priority,
                            bool enabled, const std::string& kindName, const std::string& schemaId,
                            const std::string& metadataJson) {
    lastError_.clear();
    ImplementationKind kind;
    if (domain.empty() || name.empty())
        lastError_ = "policy domain and name must not be empty";
    else if (version <= 0)
        lastError_ = "policy version must be positive";
    else if (!parseImplementationKind(kindName, kind))
        lastError_ = "invalid implementation kind";
    const Key  key{domain, name};
    const bool exists = descriptors_.find(key) != descriptors_.end();
    if (lastError_.empty() && replace != exists)
        lastError_ = replace ? "policy does not exist" : "policy already exists";
    std::string error;
    auto        metadata = eve::json::Document::parse(metadataJson, &error);
    if (lastError_.empty() && (!metadata.valid() || !metadata.root().isObject()))
        lastError_ = error.empty() ? "policy metadata must be a JSON object" : error;
    if (!lastError_.empty()) return false;
    auto& generation = generations_[key];
    if (generation == std::numeric_limits<uint64_t>::max() ||
        nextEventSequence_ == std::numeric_limits<uint64_t>::max()) {
        lastError_ = "policy generation or event sequence exhausted";
        return false;
    }
    ++generation;
    descriptors_[key] = {domain,    name, version, priority, enabled, kind, schemaId, canonicalJson(metadata.root()),
                         generation};
    emit(replace ? "policy_replaced" : "policy_registered", descriptors_[key]);
    return true;
}

void PolicyRegistry::emit(const std::string& eventName, const PolicyDescriptor& descriptor) {
    events_.push_back({nextEventSequence_++, eventName, descriptor.domain, descriptor.name, descriptor.generation,
                       descriptor.enabled});
}

bool PolicyRegistry::remove(const std::string& domain, const std::string& name) {
    lastError_.clear();
    const Key key{domain, name};
    auto      it = descriptors_.find(key);
    if (it == descriptors_.end()) {
        lastError_ = "policy does not exist";
        return false;
    }
    auto& generation = generations_[key];
    if (generation == std::numeric_limits<uint64_t>::max() ||
        nextEventSequence_ == std::numeric_limits<uint64_t>::max()) {
        lastError_ = "policy generation or event sequence exhausted";
        return false;
    }
    PolicyDescriptor removed = it->second;
    removed.generation       = ++generation;
    descriptors_.erase(it);
    emit("policy_removed", removed);
    return true;
}

bool PolicyRegistry::enable(const std::string& domain, const std::string& name, bool enabled) {
    lastError_.clear();
    const Key key{domain, name};
    auto      it = descriptors_.find(key);
    if (it == descriptors_.end()) {
        lastError_ = "policy does not exist";
        return false;
    }
    if (it->second.enabled == enabled) return true;
    auto& generation = generations_[key];
    if (generation == std::numeric_limits<uint64_t>::max() ||
        nextEventSequence_ == std::numeric_limits<uint64_t>::max()) {
        lastError_ = "policy generation or event sequence exhausted";
        return false;
    }
    it->second.enabled    = enabled;
    it->second.generation = ++generation;
    emit("policy_enabled", it->second);
    return true;
}

const PolicyDescriptor* PolicyRegistry::resolve(const std::string& domain, const std::string& name) const {
    const auto it = descriptors_.find({domain, name});
    return it == descriptors_.end() ? nullptr : &it->second;
}

const PolicyDescriptor* PolicyRegistry::resolveHandle(const PolicyHandle& handle) const {
    return resolveGeneration(handle.domain, handle.name, handle.generation);
}

const PolicyDescriptor* PolicyRegistry::resolveGeneration(const std::string& domain, const std::string& name,
                                                          uint64_t generation) const {
    const auto* value = resolve(domain, name);
    return value && value->generation == generation ? value : nullptr;
}

PolicyHandle PolicyRegistry::handle(const std::string& domain, const std::string& name) const {
    const auto* value = resolve(domain, name);
    return value ? PolicyHandle{domain, name, value->generation} : PolicyHandle{domain, name, 0};
}

const PolicyDescriptor* PolicyRegistry::select(const std::string& domain) const {
    const PolicyDescriptor* selected = nullptr;
    for (auto it = descriptors_.lower_bound({domain, {}}); it != descriptors_.end() && it->first.first == domain;
         ++it) {
        const auto& candidate = it->second;
        if (candidate.enabled && (!selected || candidate.priority > selected->priority ||
                                  (candidate.priority == selected->priority && candidate.name < selected->name)))
            selected = &candidate;
    }
    return selected;
}

int PolicyRegistry::size() const { return static_cast<int>(descriptors_.size()); }
int PolicyRegistry::countDomain(const std::string& domain) const {
    int count = 0;
    for (auto it = descriptors_.lower_bound({domain, {}}); it != descriptors_.end() && it->first.first == domain; ++it)
        ++count;
    return count;
}

const PolicyDescriptor* PolicyRegistry::at(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= descriptors_.size()) return nullptr;
    auto it = descriptors_.begin();
    std::advance(it, index);
    return &it->second;
}

const PolicyDescriptor* PolicyRegistry::atDomain(const std::string& domain, int index) const {
    if (index < 0) return nullptr;
    auto it = descriptors_.lower_bound({domain, {}});
    while (it != descriptors_.end() && it->first.first == domain && index-- > 0) ++it;
    return it != descriptors_.end() && it->first.first == domain ? &it->second : nullptr;
}

int                PolicyRegistry::eventCount() const { return static_cast<int>(events_.size()); }
const PolicyEvent* PolicyRegistry::eventAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}
void PolicyRegistry::clearEvents() { events_.clear(); }

std::string PolicyRegistry::snapshotJson() const {
    std::ostringstream out;
    out << "{\"descriptors\":[";
    bool first = true;
    for (const auto& [key, value] : descriptors_) {
        if (!first) out << ',';
        first = false;
        out << "{\"domain\":" << quote(value.domain) << ",\"enabled\":" << (value.enabled ? "true" : "false")
            << ",\"generation\":" << quote(std::to_string(value.generation))
            << ",\"kind\":" << quote(implementationKindName(value.kind)) << ",\"metadata\":" << value.metadataJson
            << ",\"name\":" << quote(value.name) << ",\"priority\":" << value.priority
            << ",\"schemaId\":" << quote(value.schemaId) << ",\"version\":" << value.version << '}';
    }
    out << "],\"events\":[";
    first = true;
    for (const auto& event : events_) {
        if (!first) out << ',';
        first = false;
        out << "{\"domain\":" << quote(event.domain) << ",\"enabled\":" << (event.enabled ? "true" : "false")
            << ",\"generation\":" << quote(std::to_string(event.generation)) << ",\"name\":" << quote(event.name)
            << ",\"policyName\":" << quote(event.policyName)
            << ",\"sequence\":" << quote(std::to_string(event.sequence)) << '}';
    }
    out << "],\"generations\":[";
    first = true;
    for (const auto& [key, generation] : generations_) {
        if (!first) out << ',';
        first = false;
        out << "{\"domain\":" << quote(key.first) << ",\"generation\":" << quote(std::to_string(generation))
            << ",\"name\":" << quote(key.second) << '}';
    }
    return out.str() + "],\"nextEventSequence\":" + quote(std::to_string(nextEventSequence_)) + ",\"version\":1}";
}

bool PolicyRegistry::restoreJson(const std::string& json) {
    lastError_.clear();
    std::string error;
    auto        document = eve::json::Document::parse(json, &error);
    auto        root     = document.root();
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1) {
        lastError_ = error.empty() ? "invalid policy snapshot" : error;
        return false;
    }
    auto     descriptors  = root.get("descriptors");
    auto     generations  = root.get("generations");
    auto     events       = root.get("events");
    uint64_t nextSequence = 0;
    if (!descriptors.isArray() || !generations.isArray() || !events.isArray() ||
        !parseU64(root.get("nextEventSequence"), nextSequence) || nextSequence == 0) {
        lastError_ = "invalid policy snapshot fields";
        return false;
    }
    std::map<Key, uint64_t>         restoredGenerations;
    std::map<Key, PolicyDescriptor> restoredDescriptors;
    std::vector<PolicyEvent>        restoredEvents;
    for (size_t i = 0; i < generations.size(); ++i) {
        auto     value = generations.at(i);
        Key      key{value.getString("domain"), value.getString("name")};
        uint64_t generation = 0;
        if (!value.isObject() || key.first.empty() || key.second.empty() ||
            !parseU64(value.get("generation"), generation) || generation == 0 ||
            !restoredGenerations.emplace(key, generation).second) {
            lastError_ = "invalid policy generation at index " + std::to_string(i);
            return false;
        }
    }
    for (size_t i = 0; i < descriptors.size(); ++i) {
        auto             value = descriptors.at(i);
        PolicyDescriptor descriptor;
        descriptor.domain    = value.getString("domain");
        descriptor.name      = value.getString("name");
        std::string kindName = value.getString("kind");
        if (!value.isObject() || descriptor.domain.empty() || descriptor.name.empty() ||
            !parseInt(value.get("version"), descriptor.version, true) ||
            !parseInt(value.get("priority"), descriptor.priority, false) || !value.get("enabled").isBool() ||
            !parseImplementationKind(kindName, descriptor.kind) || !value.get("metadata").isObject() ||
            !parseU64(value.get("generation"), descriptor.generation) || descriptor.generation == 0) {
            lastError_ = "invalid policy descriptor at index " + std::to_string(i);
            return false;
        }
        descriptor.enabled      = value.getBool("enabled");
        descriptor.schemaId     = value.getString("schemaId");
        descriptor.metadataJson = canonicalJson(value.get("metadata"));
        Key  key{descriptor.domain, descriptor.name};
        auto generation = restoredGenerations.find(key);
        if (generation == restoredGenerations.end() || generation->second != descriptor.generation ||
            !restoredDescriptors.emplace(key, descriptor).second) {
            lastError_ = "inconsistent policy generation at index " + std::to_string(i);
            return false;
        }
    }
    uint64_t previous = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        auto        value = events.at(i);
        PolicyEvent event;
        event.name       = value.getString("name");
        event.domain     = value.getString("domain");
        event.policyName = value.getString("policyName");
        if (!value.isObject() || event.domain.empty() || event.policyName.empty() ||
            (event.name != "policy_registered" && event.name != "policy_replaced" && event.name != "policy_removed" &&
             event.name != "policy_enabled") ||
            !value.get("enabled").isBool() || !parseU64(value.get("generation"), event.generation) ||
            event.generation == 0 || !parseU64(value.get("sequence"), event.sequence) || event.sequence <= previous) {
            lastError_ = "invalid policy event at index " + std::to_string(i);
            return false;
        }
        event.enabled = value.getBool("enabled");
        previous      = event.sequence;
        restoredEvents.push_back(std::move(event));
    }
    if (previous >= nextSequence) {
        lastError_ = "next event sequence must exceed retained events";
        return false;
    }
    descriptors_       = std::move(restoredDescriptors);
    generations_       = std::move(restoredGenerations);
    events_            = std::move(restoredEvents);
    nextEventSequence_ = nextSequence;
    return true;
}

const std::string& PolicyRegistry::lastError() const { return lastError_; }

PolicyRegistry* PolicyRegistryModule::newRegistry() {
    registries_.push_back(std::make_unique<PolicyRegistry>());
    return registries_.back().get();
}

Module_IMPL(PolicyRegistryModule, new PolicyRegistryModule());

void PolicyRegistryModule::expose(ssq::Table& table) {
    auto descriptor = table.addClass<PolicyDescriptor>(
        "PolicyDescriptor", std::function<PolicyDescriptor*()>([]() -> PolicyDescriptor* { return nullptr; }), false);
    descriptor.addFunc("getDomain", [](PolicyDescriptor* v) { return v ? v->domain : std::string{}; });
    descriptor.addFunc("getName", [](PolicyDescriptor* v) { return v ? v->name : std::string{}; });
    descriptor.addFunc("getVersion", [](PolicyDescriptor* v) { return v ? v->version : 0; });
    descriptor.addFunc("getPriority", [](PolicyDescriptor* v) { return v ? v->priority : 0; });
    descriptor.addFunc("isEnabled", [](PolicyDescriptor* v) { return v && v->enabled; });
    descriptor.addFunc("getKind",
                       [](PolicyDescriptor* v) { return v ? implementationKindName(v->kind) : std::string{}; });
    descriptor.addFunc("getSchemaId", [](PolicyDescriptor* v) { return v ? v->schemaId : std::string{}; });
    descriptor.addFunc("getMetadataJson", [](PolicyDescriptor* v) { return v ? v->metadataJson : std::string{}; });
    descriptor.addFunc("getGeneration",
                       [](PolicyDescriptor* v) { return v ? static_cast<int64_t>(v->generation) : int64_t{0}; });

    auto event = table.addClass<PolicyEvent>(
        "PolicyEvent", std::function<PolicyEvent*()>([]() -> PolicyEvent* { return nullptr; }), false);
    event.addFunc("getSequence", [](PolicyEvent* v) { return v ? static_cast<int64_t>(v->sequence) : int64_t{0}; });
    event.addFunc("getName", [](PolicyEvent* v) { return v ? v->name : std::string{}; });
    event.addFunc("getDomain", [](PolicyEvent* v) { return v ? v->domain : std::string{}; });
    event.addFunc("getPolicyName", [](PolicyEvent* v) { return v ? v->policyName : std::string{}; });
    event.addFunc("getGeneration", [](PolicyEvent* v) { return v ? static_cast<int64_t>(v->generation) : int64_t{0}; });
    event.addFunc("isEnabled", [](PolicyEvent* v) { return v && v->enabled; });

    auto registry = table.addClass<PolicyRegistry>(
        "PolicyRegistry", std::function<PolicyRegistry*()>([]() -> PolicyRegistry* { return nullptr; }), false);
    registry.addFunc("registerPolicy", &PolicyRegistry::registerPolicy);
    registry.addFunc("replacePolicy", &PolicyRegistry::replacePolicy);
    registry.addFunc("remove", &PolicyRegistry::remove);
    registry.addFunc("enable", &PolicyRegistry::enable);
    registry.addFunc("resolve", [](PolicyRegistry* v, const std::string& d, const std::string& n) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->resolve(d, n)) : nullptr;
    });
    registry.addFunc(
        "resolveGeneration",
        [](PolicyRegistry* v, const std::string& d, const std::string& n, int64_t g) -> PolicyDescriptor* {
            return v && g > 0 ? const_cast<PolicyDescriptor*>(v->resolveGeneration(d, n, static_cast<uint64_t>(g)))
                              : nullptr;
        });
    registry.addFunc("select", [](PolicyRegistry* v, const std::string& d) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->select(d)) : nullptr;
    });
    registry.addFunc("size", &PolicyRegistry::size);
    registry.addFunc("countDomain", &PolicyRegistry::countDomain);
    registry.addFunc("at", [](PolicyRegistry* v, int i) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->at(i)) : nullptr;
    });
    registry.addFunc("atDomain", [](PolicyRegistry* v, const std::string& d, int i) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->atDomain(d, i)) : nullptr;
    });
    registry.addFunc("eventCount", &PolicyRegistry::eventCount);
    registry.addFunc("eventAt", [](PolicyRegistry* v, int i) -> PolicyEvent* {
        return v ? const_cast<PolicyEvent*>(v->eventAt(i)) : nullptr;
    });
    registry.addFunc("clearEvents", &PolicyRegistry::clearEvents);
    registry.addFunc("snapshotJson", &PolicyRegistry::snapshotJson);
    registry.addFunc("restoreJson", &PolicyRegistry::restoreJson);
    registry.addFunc("lastError", &PolicyRegistry::lastError);

    auto cls = table.addClass(name, PolicyRegistryModule::create, false);
    expose(cls);
}

void PolicyRegistryModule::expose(ssq::Class& cls) {
    cls.addFunc("getName", &PolicyRegistryModule::getName);
    cls.addFunc("newRegistry", &PolicyRegistryModule::newRegistry);
}

}  // namespace eve::policyregistry
