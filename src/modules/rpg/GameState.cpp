#include "rpg/GameState.h"

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

GameState &GameState::global() {
    static GameState g;
    return g;
}

}  // namespace eve::rpg