#include "dialogue/ConversationPersistence.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <typeinfo>

namespace eve::dialogue {
namespace {

Poco::Dynamic::Var stateToVar(const StateValue& state) {
    switch (state.kind()) {
        case StateValue::Kind::Null: return {};
        case StateValue::Kind::Int: return Poco::Dynamic::Var(static_cast<Poco::Int64>(state.asInt()));
        case StateValue::Kind::Float: return Poco::Dynamic::Var(state.asDouble());
        case StateValue::Kind::Bool: return Poco::Dynamic::Var(state.asBool());
        case StateValue::Kind::String: return Poco::Dynamic::Var(state.asString());
        case StateValue::Kind::Array: {
            Poco::JSON::Array::Ptr array(new Poco::JSON::Array());
            for (size_t i = 0; i < state.arraySize(); ++i) array->add(stateToVar(state.at(i)));
            return Poco::Dynamic::Var(array);
        }
        case StateValue::Kind::Object: {
            Poco::JSON::Object::Ptr object(new Poco::JSON::Object());
            for (const auto& key : state.keys()) object->set(key, stateToVar(*state.find(key)));
            return Poco::Dynamic::Var(object);
        }
    }
    return {};
}

StateValue varToState(const Poco::Dynamic::Var& value) {
    if (value.isEmpty()) return StateValue::null();
    if (value.isBoolean()) return StateValue::boolean(value.convert<bool>());
    if (value.isInteger()) return StateValue::integer(value.convert<Poco::Int64>());
    if (value.isNumeric()) return StateValue::number(value.convert<double>());
    if (value.isString()) return StateValue::string(value.convert<std::string>());
    if (value.type() == typeid(Poco::JSON::Array::Ptr)) {
        StateValue             result = StateValue::array();
        Poco::JSON::Array::Ptr array  = value.extract<Poco::JSON::Array::Ptr>();
        if (array)
            for (size_t i = 0; i < array->size(); ++i)
                result.pushBack(varToState(array->get(static_cast<unsigned int>(i))));
        return result;
    }
    if (value.type() == typeid(Poco::JSON::Object::Ptr)) {
        StateValue              result = StateValue::object();
        Poco::JSON::Object::Ptr object = value.extract<Poco::JSON::Object::Ptr>();
        if (object)
            for (const auto& key : object->getNames()) result.set(key, varToState(object->get(key)));
        return result;
    }
    return StateValue::null();
}

std::string trim(std::string text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last  = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) { return std::isspace(c); });
    return first >= last.base() ? std::string{} : std::string(first, last.base());
}

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

std::string conversationStateToJson(const StateValue& state, std::string* error) {
    try {
        std::ostringstream output;
        Poco::JSON::Stringifier::stringify(stateToVar(state), output);
        return output.str();
    } catch (const Poco::Exception& e) {
        if (error) *error = e.displayText();
    } catch (const std::exception& e) {
        if (error) *error = e.what();
    }
    return {};
}

bool conversationStateFromJson(const std::string& json, StateValue& state, std::string* error) {
    try {
        state = varToState(Poco::JSON::Parser().parse(json));
        if (!state.isObject()) return fail(error, "conversation: save JSON root must be an object");
        return true;
    } catch (const Poco::Exception& e) {
        return fail(error, e.displayText());
    } catch (const std::exception& e) {
        return fail(error, e.what());
    }
}

bool ConversationSaveMigrations::registerMigration(const std::string& assetId, int fromVersion,
                                                   const std::string& currentAssetId, const std::string& nodeMap,
                                                   std::string* error) {
    if (assetId.empty() || currentAssetId.empty() || fromVersion < 0)
        return fail(error, "conversation: migration requires asset IDs and a non-negative version");
    Rule               rule{assetId, fromVersion, currentAssetId, {}};
    std::istringstream mappings(nodeMap);
    for (std::string item; std::getline(mappings, item, ',');) {
        item             = trim(std::move(item));
        const size_t pos = item.find(':');
        if (item.empty()) continue;
        if (pos == std::string::npos || trim(item.substr(0, pos)).empty() || trim(item.substr(pos + 1)).empty())
            return fail(error, "conversation: node migration must use old:new pairs");
        rule.nodes[trim(item.substr(0, pos))] = trim(item.substr(pos + 1));
    }
    auto existing = std::find_if(rules_.begin(), rules_.end(), [&](const Rule& candidate) {
        return candidate.assetId == assetId && candidate.fromVersion == fromVersion;
    });
    if (existing == rules_.end())
        rules_.push_back(std::move(rule));
    else
        *existing = std::move(rule);
    return true;
}

bool ConversationSaveMigrations::migrate(StateValue& state, const Resolver& resolve, std::string* error) const {
    StateValue candidate    = state;
    auto       migrateFrame = [&](StateValue& frame) {
        StateValue* asset   = frame.find("asset");
        StateValue* version = frame.find("version");
        StateValue* node    = frame.find("node");
        if (!asset || !asset->isString() || !version || !version->isInt() || !node || !node->isString())
            return fail(error, "conversation: saved frame is malformed");
        const ConversationAsset* current = resolve(asset->asString());
        if (current && current->version == version->asInt()) return true;
        const auto rule = std::find_if(rules_.begin(), rules_.end(), [&](const Rule& item) {
            return item.assetId == asset->asString() && item.fromVersion == version->asInt();
        });
        if (rule == rules_.end())
            return fail(error, "conversation: no migration for '" + asset->asString() + "' version " +
                                   std::to_string(version->asInt()));
        current = resolve(rule->currentAssetId);
        if (!current) return fail(error, "conversation: migration target '" + rule->currentAssetId + "' is missing");
        std::string currentNode = node->asString();
        if (const auto renamed = rule->nodes.find(currentNode); renamed != rule->nodes.end())
            currentNode = renamed->second;
        if (!current->findNode(currentNode))
            return fail(error, "conversation: migrated node '" + currentNode + "' is missing");
        frame.set("asset", StateValue::string(current->id));
        frame.set("version", StateValue::integer(current->version));
        frame.set("node", StateValue::string(std::move(currentNode)));
        return true;
    };

    StateValue* active = candidate.find("active");
    if (!active || !active->isBool()) return fail(error, "conversation: state is missing active");
    if (active->asBool()) {
        StateValue* current = candidate.find("current");
        StateValue* stack   = candidate.find("stack");
        if (!current || !current->isObject() || !stack || !stack->isArray())
            return fail(error, "conversation: state frames are malformed");
        if (!migrateFrame(*current)) return false;
        for (size_t i = 0; i < stack->arraySize(); ++i)
            if (!migrateFrame(stack->at(i))) return false;
    }
    state = std::move(candidate);
    return true;
}

}  // namespace eve::dialogue
