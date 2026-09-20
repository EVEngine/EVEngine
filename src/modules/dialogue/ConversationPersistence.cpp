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

template <class T = void>
eve::Result<T> persistenceFailure(eve::DiagnosticCode code, const std::string& message, const std::string& path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, message, path, {}, "dialogue.persistence"));
}

}  // namespace

eve::Result<std::string> conversationStateToJson(const StateValue& state) {
    try {
        std::ostringstream output;
        Poco::JSON::Stringifier::stringify(stateToVar(state), output);
        return eve::Result<std::string>::success(output.str());
    } catch (const Poco::Exception& e) {
        return persistenceFailure<std::string>(eve::DiagnosticCode::SerializationError, e.displayText(), "save");
    } catch (const std::exception& e) {
        return persistenceFailure<std::string>(eve::DiagnosticCode::SerializationError, e.what(), "save");
    }
}

eve::Result<StateValue> conversationStateFromJson(const std::string& json) {
    try {
        StateValue state = varToState(Poco::JSON::Parser().parse(json));
        if (!state.isObject())
            return persistenceFailure<StateValue>(eve::DiagnosticCode::ParseError,
                                                  "conversation: save JSON root must be an object", "save");
        return eve::Result<StateValue>::success(std::move(state));
    } catch (const Poco::Exception& e) {
        return persistenceFailure<StateValue>(eve::DiagnosticCode::ParseError, e.displayText(), "save");
    } catch (const std::exception& e) {
        return persistenceFailure<StateValue>(eve::DiagnosticCode::ParseError, e.what(), "save");
    }
}

eve::Result<void> ConversationSaveMigrations::registerMigration(const std::string& assetId, int fromVersion,
                                                                const std::string& currentAssetId,
                                                                const std::string& nodeMap) {
    if (assetId.empty() || currentAssetId.empty() || fromVersion < 0)
        return persistenceFailure(eve::DiagnosticCode::InvalidArgument,
                                  "conversation: migration requires asset IDs and a non-negative version",
                                  "migration");
    Rule               rule{assetId, fromVersion, currentAssetId, {}};
    std::istringstream mappings(nodeMap);
    for (std::string item; std::getline(mappings, item, ',');) {
        item             = trim(std::move(item));
        const size_t pos = item.find(':');
        if (item.empty()) continue;
        if (pos == std::string::npos || trim(item.substr(0, pos)).empty() || trim(item.substr(pos + 1)).empty())
            return persistenceFailure(eve::DiagnosticCode::InvalidArgument,
                                      "conversation: node migration must use old:new pairs", "migration.nodeMap");
        rule.nodes[trim(item.substr(0, pos))] = trim(item.substr(pos + 1));
    }
    auto existing = std::find_if(rules_.begin(), rules_.end(), [&](const Rule& candidate) {
        return candidate.assetId == assetId && candidate.fromVersion == fromVersion;
    });
    if (existing == rules_.end())
        rules_.push_back(std::move(rule));
    else
        *existing = std::move(rule);
    return eve::Result<void>::success();
}

eve::Result<StateValue> ConversationSaveMigrations::migrate(const StateValue& state, const Resolver& resolve) const {
    StateValue candidate    = state;
    std::string error;
    const auto failFrame = [&](std::string message) {
        error = std::move(message);
        return false;
    };
    auto       migrateFrame = [&](StateValue& frame) {
        StateValue* asset   = frame.find("asset");
        StateValue* version = frame.find("version");
        StateValue* node    = frame.find("node");
        if (!asset || !asset->isString() || !version || !version->isInt() || !node || !node->isString())
            return failFrame("conversation: saved frame is malformed");
        const ConversationAsset* current = resolve(asset->asString());
        if (current && current->version == version->asInt()) return true;
        const auto rule = std::find_if(rules_.begin(), rules_.end(), [&](const Rule& item) {
            return item.assetId == asset->asString() && item.fromVersion == version->asInt();
        });
        if (rule == rules_.end())
            return failFrame("conversation: no migration for '" + asset->asString() + "' version " +
                             std::to_string(version->asInt()));
        current = resolve(rule->currentAssetId);
        if (!current) {
            error = "conversation: migration target '" + rule->currentAssetId + "' is missing";
            return false;
        }
        std::string currentNode = node->asString();
        if (const auto renamed = rule->nodes.find(currentNode); renamed != rule->nodes.end())
            currentNode = renamed->second;
        if (!current->findNode(currentNode))
            return failFrame("conversation: migrated node '" + currentNode + "' is missing");
        frame.set("asset", StateValue::string(current->id));
        frame.set("version", StateValue::integer(current->version));
        frame.set("node", StateValue::string(std::move(currentNode)));
        return true;
    };

    StateValue* active = candidate.find("active");
    if (!active || !active->isBool())
        return persistenceFailure<StateValue>(eve::DiagnosticCode::SerializationError,
                                              "conversation: state is missing active", "save.active");
    if (active->asBool()) {
        StateValue* current = candidate.find("current");
        StateValue* stack   = candidate.find("stack");
        if (!current || !current->isObject() || !stack || !stack->isArray())
            return persistenceFailure<StateValue>(eve::DiagnosticCode::SerializationError,
                                                  "conversation: state frames are malformed", "save.frames");
        if (!migrateFrame(*current))
            return persistenceFailure<StateValue>(eve::DiagnosticCode::UnknownVersion, error, "save.current");
        for (size_t i = 0; i < stack->arraySize(); ++i)
            if (!migrateFrame(stack->at(i)))
                return persistenceFailure<StateValue>(eve::DiagnosticCode::UnknownVersion, error, "save.stack");
    }
    return eve::Result<StateValue>::success(std::move(candidate));
}

}  // namespace eve::dialogue
