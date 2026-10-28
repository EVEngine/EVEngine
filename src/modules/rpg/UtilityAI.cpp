#include "rpg/UtilityAI.h"
#include "common/Json.h"

#include <algorithm>
#include <cmath>

namespace eve::rpg {
namespace {
std::unordered_map<std::string, AIDecisionSetDefinition>& sets() {
    static std::unordered_map<std::string, AIDecisionSetDefinition> v;
    return v;
}
AIDecisionSetDefinition parse(eve::json::Value value) {
    AIDecisionSetDefinition set;
    set.id       = value.getString("id");
    auto actions = value.get("actions");
    for (size_t i = 0; i < actions.size(); ++i) {
        auto               a = actions.at(i);
        AIActionDefinition action;
        action.id           = a.getString("id");
        action.weight       = a.getDouble("weight", 1.0);
        auto considerations = a.get("considerations");
        for (size_t j = 0; j < considerations.size(); ++j) {
            auto            c = considerations.at(j);
            AIConsideration item;
            item.input  = c.getString("input");
            item.curve  = c.getString("curve", "linear");
            item.min    = c.getDouble("min", 0.0);
            item.max    = c.getDouble("max", 1.0);
            item.weight = c.getDouble("weight", 1.0);
            if (!item.input.empty()) action.considerations.push_back(std::move(item));
        }
        if (!action.id.empty()) set.actions.push_back(std::move(action));
    }
    return set;
}
double response(const AIConsideration& c, double input) {
    double range = c.max - c.min;
    double x     = range == 0.0 ? (input >= c.max ? 1.0 : 0.0) : std::clamp((input - c.min) / range, 0.0, 1.0);
    if (c.curve == "inverse")
        x = 1.0 - x;
    else if (c.curve == "quadratic")
        x *= x;
    else if (c.curve == "sqrt")
        x = std::sqrt(x);
    else if (c.curve == "threshold")
        x = x >= 0.5 ? 1.0 : 0.0;
    return std::clamp(x * c.weight, 0.0, 1.0);
}
}  // namespace
bool AIDecisionRegistry::registerSet(const AIDecisionSetDefinition& d) {
    if (d.id.empty() || d.actions.empty()) return false;
    sets()[d.id] = d;
    return true;
}
const AIDecisionSetDefinition* AIDecisionRegistry::find(const std::string& id) {
    auto i = sets().find(id);
    return i == sets().end() ? nullptr : &i->second;
}
void AIDecisionRegistry::clear() { sets().clear(); }
int  AIDecisionRegistry::count() { return int(sets().size()); }
int  AIDecisionRegistry::loadFromJson(const std::string& json, std::string* error) {
    std::string e;
    auto        doc = eve::json::Document::parse(json, &e);
    if (!doc.valid()) {
        if (error) *error = e;
        return 0;
    }
    auto root = doc.root();
    int  n    = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i)
            if (registerSet(parse(root.at(i)))) ++n;
    } else if (root.isObject() && registerSet(parse(root)))
        ++n;
    return n;
}
float UtilityBrain::getInput(const std::string& name, float fallback) const {
    auto i = inputs_.find(name);
    return i == inputs_.end() ? fallback : float(i->second);
}
std::string UtilityBrain::chooseAction() {
    auto* set  = AIDecisionRegistry::find(decisionSet_);
    lastScore_ = 0.0;
    if (!set) return {};
    std::string best;
    for (const auto& a : set->actions) {
        double score = std::max(0.0, a.weight);
        for (const auto& c : a.considerations) {
            auto i = inputs_.find(c.input);
            score *= response(c, i == inputs_.end() ? 0.0 : i->second);
        }
        if (best.empty() || score > lastScore_) {
            best       = a.id;
            lastScore_ = score;
        }
    }
    return best;
}
}  // namespace eve::rpg
