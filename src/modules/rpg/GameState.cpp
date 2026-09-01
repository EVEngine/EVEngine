#include "rpg/GameState.h"

#include "common/Value.h"

#include <cmath>

namespace {

template <typename T>
eve::Result<T> gameStateFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<std::unordered_map<std::string, double>> parseNumbers(const eve::Value *value,
                                                                  const std::string &path) {
    if (!value || !value->isObject())
        return gameStateFailure<std::unordered_map<std::string, double>>(
            eve::DiagnosticCode::ParseError, "game state field must be an object", path);
    std::unordered_map<std::string, double> result;
    for (const auto &key : value->keys()) {
        const eve::Value *item = value->find(key);
        if (!item || !item->isNumeric())
            return gameStateFailure<std::unordered_map<std::string, double>>(
                eve::DiagnosticCode::ParseError, "game state variable must be numeric", path + "." + key);
        const double number = item->isInt64() ? static_cast<double>(item->asInt()) : item->asDouble();
        if (!std::isfinite(number))
            return gameStateFailure<std::unordered_map<std::string, double>>(
                eve::DiagnosticCode::InvalidArgument, "game state variable must be finite", path + "." + key);
        result.emplace(key, number);
    }
    return eve::Result<std::unordered_map<std::string, double>>::success(std::move(result));
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

void GameState::clear() {
    switches_.clear();
    variables_.clear();
    selfVariables_.clear();
}

eve::Result<std::string> GameState::snapshotJson() const {
    eve::Value::Object switches;
    for (const auto &[name, on] : switches_) switches.emplace(name, eve::Value(on));
    eve::Value::Object variables;
    for (const auto &[name, value] : variables_) variables.emplace(name, eve::Value(value));
    eve::Value::Object scoped;
    for (const auto &[scope, values] : selfVariables_) {
        eve::Value::Object encoded;
        for (const auto &[name, value] : values) encoded.emplace(name, eve::Value(value));
        scoped.emplace(scope, eve::Value(std::move(encoded)));
    }
    eve::Value::Object root;
    root.emplace("schema", eve::Value("eve.rpg.game-state"));
    root.emplace("selfVariables", eve::Value(std::move(scoped)));
    root.emplace("switches", eve::Value(std::move(switches)));
    root.emplace("variables", eve::Value(std::move(variables)));
    root.emplace("version", eve::Value(1));
    return eve::Value(std::move(root)).toJson();
}

eve::Result<void> GameState::restoreSnapshotJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    const eve::Value &root = parsed.value();
    if (!root.isObject())
        return gameStateFailure<void>(eve::DiagnosticCode::ParseError, "game state root must be an object", "$.");
    const eve::Value *schema = root.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.rpg.game-state")
        return gameStateFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                      "snapshot does not belong to RPG GameState", "$.schema");
    const eve::Value *version = root.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return gameStateFailure<void>(eve::DiagnosticCode::UnknownVersion,
                                      "unsupported RPG GameState snapshot version", "$.version");

    const eve::Value *switches = root.find("switches");
    if (!switches || !switches->isObject())
        return gameStateFailure<void>(eve::DiagnosticCode::ParseError,
                                      "game state switches must be an object", "$.switches");
    std::unordered_map<std::string, bool> candidateSwitches;
    for (const auto &key : switches->keys()) {
        const eve::Value *item = switches->find(key);
        if (!item || !item->isBool())
            return gameStateFailure<void>(eve::DiagnosticCode::ParseError,
                                          "game state switch must be boolean", "$.switches." + key);
        candidateSwitches.emplace(key, item->asBool());
    }

    auto candidateVariables = parseNumbers(root.find("variables"), "$.variables");
    if (!candidateVariables.ok()) return eve::Result<void>::failure(candidateVariables.status());
    const eve::Value *scoped = root.find("selfVariables");
    if (!scoped || !scoped->isObject())
        return gameStateFailure<void>(eve::DiagnosticCode::ParseError,
                                      "game state selfVariables must be an object", "$.selfVariables");
    std::unordered_map<std::string, std::unordered_map<std::string, double>> candidateScoped;
    for (const auto &scope : scoped->keys()) {
        auto values = parseNumbers(scoped->find(scope), "$.selfVariables." + scope);
        if (!values.ok()) return eve::Result<void>::failure(values.status());
        candidateScoped.emplace(scope, std::move(values).takeValue());
    }

    switches_      = std::move(candidateSwitches);
    variables_     = std::move(candidateVariables).takeValue();
    selfVariables_ = std::move(candidateScoped);
    return eve::Result<void>::success();
}

GameState &GameState::global() {
    static GameState g;
    return g;
}

}  // namespace eve::rpg
