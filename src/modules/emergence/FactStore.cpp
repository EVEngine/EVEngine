#include "emergence/FactStore.h"

#include "common/Diagnostic.h"

#include <utility>

namespace eve::emergence {
namespace {

const char* domainPrefix(FactDomain domain) {
    switch (domain) {
        case FactDomain::Value: return "value:";
        case FactDomain::Tag: return "tag:";
        case FactDomain::Attribute: return "attr:";
        case FactDomain::Resource: return "resource:";
        case FactDomain::State: return "state:";
        case FactDomain::Authority: return "authority:";
        case FactDomain::Policy: return "policy:";
    }
    return "value:";
}

}  // namespace

std::string makeFactKey(FactDomain domain, std::string_view name) {
    if (name.empty()) return {};
    std::string key(domainPrefix(domain));
    key.append(name);
    return key;
}

eve::Result<std::pair<FactDomain, std::string>> parseFactKey(std::string_view key) {
    const auto fail = [](const char* message) {
        return eve::Result<std::pair<FactDomain, std::string>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, message, "key", {}, "emergence.fact"));
    };
    if (key.empty()) return fail("fact key must be non-empty");
    const auto slash = key.find(':');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= key.size())
        return fail("fact key must use domain:name form");
    const auto domainText = key.substr(0, slash);
    const auto name       = key.substr(slash + 1);
    FactDomain domain     = FactDomain::Value;
    if (domainText == "value")
        domain = FactDomain::Value;
    else if (domainText == "tag")
        domain = FactDomain::Tag;
    else if (domainText == "attr")
        domain = FactDomain::Attribute;
    else if (domainText == "resource")
        domain = FactDomain::Resource;
    else if (domainText == "state")
        domain = FactDomain::State;
    else if (domainText == "authority")
        domain = FactDomain::Authority;
    else if (domainText == "policy")
        domain = FactDomain::Policy;
    else
        return fail("unknown fact domain prefix");
    return eve::Result<std::pair<FactDomain, std::string>>::success(
        std::make_pair(domain, std::string(name)));
}

template <class Map, class T>
bool FactStore::assign(Map& map, std::string key, T value) {
    auto it = map.find(key);
    if (it != map.end()) {
        if (it->second == value) return false;
        it->second = std::move(value);
        return true;
    }
    map.emplace(std::move(key), std::move(value));
    return true;
}

void FactStore::clear() {
    values_.clear();
    tags_.clear();
    attributes_.clear();
    resources_.clear();
    states_.clear();
    authorities_.clear();
    policies_.clear();
}

bool FactStore::setValue(std::string key, eve::Value value) {
    if (key.empty()) return false;
    return assign(values_, std::move(key), std::move(value));
}

bool FactStore::setTag(std::string tag, bool present) {
    if (tag.empty()) return false;
    return assign(tags_, std::move(tag), present);
}

bool FactStore::setAttribute(std::string key, eve::Value value) {
    if (key.empty()) return false;
    return assign(attributes_, std::move(key), std::move(value));
}

bool FactStore::setResource(std::string key, eve::Value value) {
    if (key.empty()) return false;
    return assign(resources_, std::move(key), std::move(value));
}

bool FactStore::setState(std::string key, eve::Value value) {
    if (key.empty()) return false;
    return assign(states_, std::move(key), std::move(value));
}

bool FactStore::setAuthority(std::string scope, bool granted) {
    if (scope.empty()) return false;
    return assign(authorities_, std::move(scope), granted);
}

bool FactStore::setPolicy(std::string name, decision::ConditionResult result) {
    if (name.empty()) return false;
    auto it = policies_.find(name);
    if (it != policies_.end()) {
        if (it->second.passed() == result.passed() && it->second.reasonCode() == result.reasonCode() &&
            it->second.evidence() == result.evidence())
            return false;
        it->second = std::move(result);
        return true;
    }
    policies_.emplace(std::move(name), std::move(result));
    return true;
}

bool FactStore::clearValue(std::string_view key) {
    return values_.erase(std::string(key)) > 0;
}

bool FactStore::clearTag(std::string_view tag) {
    return tags_.erase(std::string(tag)) > 0;
}

std::optional<eve::Value> FactStore::value(std::string_view key) const {
    auto it = values_.find(std::string(key));
    return it == values_.end() ? std::nullopt : std::optional<eve::Value>(it->second);
}

std::optional<bool> FactStore::hasTag(std::string_view tag) const {
    auto it = tags_.find(std::string(tag));
    return it == tags_.end() ? std::nullopt : std::optional<bool>(it->second);
}

std::optional<eve::Value> FactStore::attribute(std::string_view key) const {
    auto it = attributes_.find(std::string(key));
    return it == attributes_.end() ? std::nullopt : std::optional<eve::Value>(it->second);
}

std::optional<eve::Value> FactStore::resource(std::string_view key) const {
    auto it = resources_.find(std::string(key));
    return it == resources_.end() ? std::nullopt : std::optional<eve::Value>(it->second);
}

std::optional<eve::Value> FactStore::state(std::string_view key) const {
    auto it = states_.find(std::string(key));
    return it == states_.end() ? std::nullopt : std::optional<eve::Value>(it->second);
}

std::optional<bool> FactStore::authority(std::string_view scope) const {
    auto it = authorities_.find(std::string(scope));
    return it == authorities_.end() ? std::nullopt : std::optional<bool>(it->second);
}

std::optional<decision::ConditionResult> FactStore::policy(std::string_view name, const eve::Value&) const {
    auto it = policies_.find(std::string(name));
    return it == policies_.end() ? std::nullopt : std::optional<decision::ConditionResult>(it->second);
}

std::string FactStore::snapshotJson() const {
    eve::Value::Object root;
    eve::Value::Object values;
    for (const auto& [key, value] : values_) values.emplace(key, value);
    eve::Value::Object tags;
    for (const auto& [key, value] : tags_) tags.emplace(key, eve::Value(value));
    eve::Value::Object attributes;
    for (const auto& [key, value] : attributes_) attributes.emplace(key, value);
    eve::Value::Object resources;
    for (const auto& [key, value] : resources_) resources.emplace(key, value);
    eve::Value::Object states;
    for (const auto& [key, value] : states_) states.emplace(key, value);
    eve::Value::Object authorities;
    for (const auto& [key, value] : authorities_) authorities.emplace(key, eve::Value(value));
    root.emplace("schema", eve::Value("eve.emergence.facts"));
    root.emplace("version", eve::Value(std::int64_t{1}));
    root.emplace("values", eve::Value(std::move(values)));
    root.emplace("tags", eve::Value(std::move(tags)));
    root.emplace("attributes", eve::Value(std::move(attributes)));
    root.emplace("resources", eve::Value(std::move(resources)));
    root.emplace("states", eve::Value(std::move(states)));
    root.emplace("authorities", eve::Value(std::move(authorities)));
    auto encoded = eve::Value(std::move(root)).toJson();
    return encoded ? std::move(encoded).takeValue() : std::string("{}");
}

eve::Result<void> FactStore::restoreJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed)
        return eve::Result<void>::failure(parsed.status());
    const auto* object = parsed.value().getIf<eve::Value::Object>();
    if (object == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fact snapshot must be an object", {}, {}, "emergence.fact"));
    const auto* schema = object->find("schema") == object->end() ? nullptr : &object->at("schema");
    const auto* version = object->find("version") == object->end() ? nullptr : &object->at("version");
    if (schema == nullptr || !schema->isString() || schema->asString() != "eve.emergence.facts")
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "unexpected fact snapshot schema", "schema", {}, "emergence.fact"));
    if (version == nullptr || !version->isInt64() || version->asInt() != 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported fact snapshot version", "version", {}, "emergence.fact"));

    FactStore next;
    auto loadObject = [&](const char* field, auto&& setter) -> eve::Result<void> {
        const auto it = object->find(field);
        if (it == object->end()) return eve::Result<void>::success();
        const auto* map = it->second.getIf<eve::Value::Object>();
        if (map == nullptr)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "fact field must be an object", field, {}, "emergence.fact"));
        for (const auto& [key, value] : *map) {
            if (!setter(next, key, value))
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "invalid fact entry", field, {}, "emergence.fact"));
        }
        return eve::Result<void>::success();
    };

    if (auto r = loadObject("values", [](FactStore& store, const std::string& key, const eve::Value& value) {
            (void)store.setValue(key, value);
            return true;
        });
        !r)
        return r;
    if (auto r = loadObject("tags", [](FactStore& store, const std::string& key, const eve::Value& value) {
            if (!value.isBool()) return false;
            (void)store.setTag(key, value.asBool());
            return true;
        });
        !r)
        return r;
    if (auto r = loadObject("attributes", [](FactStore& store, const std::string& key, const eve::Value& value) {
            (void)store.setAttribute(key, value);
            return true;
        });
        !r)
        return r;
    if (auto r = loadObject("resources", [](FactStore& store, const std::string& key, const eve::Value& value) {
            (void)store.setResource(key, value);
            return true;
        });
        !r)
        return r;
    if (auto r = loadObject("states", [](FactStore& store, const std::string& key, const eve::Value& value) {
            (void)store.setState(key, value);
            return true;
        });
        !r)
        return r;
    if (auto r = loadObject("authorities", [](FactStore& store, const std::string& key, const eve::Value& value) {
            if (!value.isBool()) return false;
            (void)store.setAuthority(key, value.asBool());
            return true;
        });
        !r)
        return r;

    *this = std::move(next);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::emergence
