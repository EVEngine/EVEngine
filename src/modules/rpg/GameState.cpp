#include "rpg/GameState.h"
#include "common/StringMapInstances.h"

#include "rpg_game_state.pb.h"

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {

const google::protobuf::Value *findField(const google::protobuf::Struct &object, std::string_view name) {
    const auto item = object.fields().find(std::string(name));
    return item == object.fields().end() ? nullptr : &item->second;
}

eve::Result<std::unordered_map<std::string, double>> parseNumbers(const google::protobuf::Value *value,
                                                                  const std::string             &path) {
    if (!value || value->kind_case() != google::protobuf::Value::kStructValue)
        return eve::Result<std::unordered_map<std::string, double>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "game state field must be an object", path));
    std::unordered_map<std::string, double> result;
    for (const auto &[key, item] : value->struct_value().fields()) {
        if (item.kind_case() != google::protobuf::Value::kNumberValue)
            return eve::Result<std::unordered_map<std::string, double>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "game state variable must be numeric", path + "." + key));
        if (!std::isfinite(item.number_value()))
            return eve::Result<std::unordered_map<std::string, double>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "game state variable must be finite", path + "." + key));
        result.emplace(key, item.number_value());
    }
    return eve::Result<std::unordered_map<std::string, double>>::success(std::move(result));
}

eve::Result<std::unordered_map<std::string, std::string>> parseStrings(const google::protobuf::Value *value,
                                                                       const std::string             &path) {
    if (!value || value->kind_case() != google::protobuf::Value::kStructValue)
        return eve::Result<std::unordered_map<std::string, std::string>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "game state field must be an object", path));
    std::unordered_map<std::string, std::string> result;
    for (const auto &[key, item] : value->struct_value().fields()) {
        if (item.kind_case() != google::protobuf::Value::kStringValue)
            return eve::Result<std::unordered_map<std::string, std::string>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "game state string fact must be a string", path + "." + key));
        result.emplace(key, item.string_value());
    }
    return eve::Result<std::unordered_map<std::string, std::string>>::success(std::move(result));
}

}  // namespace

namespace eve::rpg {

void GameState::setSwitch(const std::string &name, bool on) { switches_[name] = on; }

void GameState::switchOn(const std::string &name) { switches_[name] = true; }

void GameState::switchOff(const std::string &name) { switches_[name] = false; }

bool GameState::isSwitchOn(const std::string &name) const {
    auto it = switches_.find(name);
    return it != switches_.end() && it->second;
}

void GameState::setVariable(const std::string &name, double value) { variables_[name] = value; }

double GameState::getVariable(const std::string &name) const {
    auto it = variables_.find(name);
    return it == variables_.end() ? 0.0 : it->second;
}

void GameState::addVariable(const std::string &name, double delta) {
    variables_[name] = getVariable(name) + delta;
}

void GameState::setSelfVariable(const std::string &scope, const std::string &name, double value) {
    selfVariables_[scope][name] = value;
}

double GameState::getSelfVariable(const std::string &scope, const std::string &name) const {
    auto it = selfVariables_.find(scope);
    if (it == selfVariables_.end()) return 0.0;
    auto vit = it->second.find(name);
    return vit == it->second.end() ? 0.0 : vit->second;
}

bool GameState::hasSelfVariable(const std::string &scope, const std::string &name) const {
    auto it = selfVariables_.find(scope);
    if (it == selfVariables_.end()) return false;
    return it->second.find(name) != it->second.end();
}

void GameState::setSelfString(const std::string &scope, const std::string &name, std::string value) {
    selfStrings_[scope][name] = std::move(value);
}

std::string GameState::getSelfString(const std::string &scope, const std::string &name) const {
    auto it = selfStrings_.find(scope);
    if (it == selfStrings_.end()) return {};
    auto sit = it->second.find(name);
    return sit == it->second.end() ? std::string{} : sit->second;
}

bool GameState::hasSelfString(const std::string &scope, const std::string &name) const {
    auto it = selfStrings_.find(scope);
    if (it == selfStrings_.end()) return false;
    return it->second.find(name) != it->second.end();
}

void GameState::clearSelfString(const std::string &scope, const std::string &name) {
    auto it = selfStrings_.find(scope);
    if (it == selfStrings_.end()) return;
    it->second.erase(name);
    if (it->second.empty()) selfStrings_.erase(it);
}

void GameState::clear() {
    switches_.clear();
    variables_.clear();
    selfVariables_.clear();
    selfStrings_.clear();
}

eve::Result<void> GameState::captureMessage(proto::GameStateSave &save) const {
    save.set_schema("eve.rpg.game-state");
    save.set_version(1);
    for (const auto &[name, on] : switches_) (*save.mutable_switches())[name] = on;
    for (const auto &[name, value] : variables_) {
        if (!std::isfinite(value))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "game state variable must be finite", name));
        (*save.mutable_variables())[name] = value;
    }
    std::vector<std::string> numericScopes;
    numericScopes.reserve(selfVariables_.size());
    for (const auto &entry : selfVariables_) numericScopes.push_back(entry.first);
    std::sort(numericScopes.begin(), numericScopes.end());
    for (const auto &scope : numericScopes) {
        const auto &values = selfVariables_.at(scope);
        auto       *facts  = save.add_self_variables();
        facts->set_scope(scope);
        for (const auto &[name, value] : values) {
            if (!std::isfinite(value))
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "game state scoped variable must be finite",
                                                                         scope + "." + name));
            (*facts->mutable_values())[name] = value;
        }
    }
    std::vector<std::string> stringScopes;
    stringScopes.reserve(selfStrings_.size());
    for (const auto &entry : selfStrings_) stringScopes.push_back(entry.first);
    std::sort(stringScopes.begin(), stringScopes.end());
    for (const auto &scope : stringScopes) {
        const auto &values = selfStrings_.at(scope);
        auto       *facts  = save.add_self_strings();
        facts->set_scope(scope);
        for (const auto &[name, value] : values) (*facts->mutable_values())[name] = value;
    }
    return eve::Result<void>::success();
}

eve::Result<std::string> GameState::snapshotBinary() const {
    proto::GameStateSave save;
    auto                 captured = captureMessage(save);
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string bytes;
    if (!save.SerializeToString(&bytes))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                        "game state Protobuf serialization failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<std::string> GameState::snapshotJson() const {
    proto::GameStateSave save;
    auto                 captured = captureMessage(save);
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string json;
    const auto  status = google::protobuf::util::MessageToJsonString(save, &json);
    if (!status.ok())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                        "game state Protobuf JSON conversion failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<void> GameState::restoreMessage(const proto::GameStateSave &save) {
    auto fail = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
    };
    if (!save.has_schema() || save.schema() != "eve.rpg.game-state")
        return fail(eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPG GameState", "$.schema");
    if (!save.has_version() || save.version() != 1)
        return fail(eve::DiagnosticCode::UnknownVersion, "unsupported RPG GameState snapshot version", "$.version");
    std::unordered_map<std::string, bool>   candidateSwitches(save.switches().begin(), save.switches().end());
    std::unordered_map<std::string, double> candidateVariables;
    for (const auto &[name, value] : save.variables()) {
        if (!std::isfinite(value))
            return fail(eve::DiagnosticCode::InvalidArgument, "game state variable must be finite",
                        "$.variables." + name);
        candidateVariables.emplace(name, value);
    }
    std::unordered_map<std::string, std::unordered_map<std::string, double>> candidateScoped;
    for (const auto &facts : save.self_variables()) {
        if (!facts.has_scope() ||
            !candidateScoped.emplace(facts.scope(), std::unordered_map<std::string, double>{}).second)
            return fail(eve::DiagnosticCode::Conflict, "game state numeric scope is missing or duplicated",
                        "$.selfVariables");
        auto &values = candidateScoped.at(facts.scope());
        for (const auto &[name, value] : facts.values()) {
            if (!std::isfinite(value))
                return fail(eve::DiagnosticCode::InvalidArgument, "game state scoped variable must be finite",
                            "$.selfVariables." + facts.scope() + "." + name);
            values.emplace(name, value);
        }
    }
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> candidateStrings;
    for (const auto &facts : save.self_strings()) {
        if (!facts.has_scope() ||
            !candidateStrings.emplace(facts.scope(), std::unordered_map<std::string, std::string>{}).second)
            return fail(eve::DiagnosticCode::Conflict, "game state string scope is missing or duplicated",
                        "$.selfStrings");
        auto &values = candidateStrings.at(facts.scope());
        for (const auto &[name, value] : facts.values()) values.emplace(name, value);
    }
    switches_.swap(candidateSwitches);
    variables_.swap(candidateVariables);
    selfVariables_.swap(candidateScoped);
    selfStrings_.swap(candidateStrings);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> GameState::restoreSnapshotBinary(std::string_view bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "game state snapshot is too large"));
    proto::GameStateSave save;
    if (!save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid game state Protobuf bytes"));
    return restoreMessage(save);
}

eve::Result<void> GameState::restoreSnapshotJson(std::string_view json) {
    proto::GameStateSave                     typed;
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;
    if (google::protobuf::util::JsonStringToMessage(std::string(json), &typed, options).ok()) {
        const auto missingScope = [](const auto &facts) { return !facts.has_scope(); };
        const bool legacyScopeShape =
            std::any_of(typed.self_variables().begin(), typed.self_variables().end(), missingScope) ||
            std::any_of(typed.self_strings().begin(), typed.self_strings().end(), missingScope);
        if (!legacyScopeShape) return restoreMessage(typed);
    }

    // Version-1 JSON used nested scope maps; parse it only to upgrade old saves.
    google::protobuf::Struct legacy;
    if (!google::protobuf::util::JsonStringToMessage(std::string(json), &legacy).ok())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "game state JSON is malformed", "$"));
    const auto *schema = findField(legacy, "schema");
    if (!schema || schema->kind_case() != google::protobuf::Value::kStringValue ||
        schema->string_value() != "eve.rpg.game-state")
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPG GameState", "$.schema"));
    const auto *version = findField(legacy, "version");
    if (!version || version->kind_case() != google::protobuf::Value::kNumberValue || version->number_value() != 1.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported RPG GameState snapshot version", "$.version"));

    const auto *switches = findField(legacy, "switches");
    if (!switches || switches->kind_case() != google::protobuf::Value::kStructValue)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "game state switches must be an object", "$.switches"));
    std::unordered_map<std::string, bool> candidateSwitches;
    for (const auto &[key, item] : switches->struct_value().fields()) {
        if (item.kind_case() != google::protobuf::Value::kBoolValue)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "game state switch must be boolean", "$.switches." + key));
        candidateSwitches.emplace(key, item.bool_value());
    }

    auto candidateVariables = parseNumbers(findField(legacy, "variables"), "$.variables");
    if (!candidateVariables.ok()) return eve::Result<void>::failure(candidateVariables.status());
    const auto *scoped = findField(legacy, "selfVariables");
    if (!scoped || scoped->kind_case() != google::protobuf::Value::kStructValue)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "game state selfVariables must be an object", "$.selfVariables"));
    std::unordered_map<std::string, std::unordered_map<std::string, double>> candidateScoped;
    for (const auto &[scope, values] : scoped->struct_value().fields()) {
        auto parsed = parseNumbers(&values, "$.selfVariables." + scope);
        if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
        candidateScoped.emplace(scope, std::move(parsed).takeValue());
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> candidateStrings;
    if (const auto *scopedStrings = findField(legacy, "selfStrings"); scopedStrings != nullptr) {
        if (scopedStrings->kind_case() != google::protobuf::Value::kStructValue)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "game state selfStrings must be an object", "$.selfStrings"));
        for (const auto &[scope, values] : scopedStrings->struct_value().fields()) {
            auto parsed = parseStrings(&values, "$.selfStrings." + scope);
            if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
            candidateStrings.emplace(scope, std::move(parsed).takeValue());
        }
    }

    auto numeric = std::move(candidateVariables).takeValue();
    switches_.swap(candidateSwitches);
    variables_.swap(numeric);
    selfVariables_.swap(candidateScoped);
    selfStrings_.swap(candidateStrings);
    return eve::Result<void>::success();
}

GameState &GameState::global() {
    static GameState g;
    return g;
}

}  // namespace eve::rpg
