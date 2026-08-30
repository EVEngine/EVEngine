#pragma once

#include "common/StateValue.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::dialogue {

/** @brief Parameter substitution and state-driven character voice modifiers. */
class ConversationTextRenderer {
public:
    using Evaluator = std::function<bool(const std::string&)>;
    struct ToneRule {
        std::string expression;
        std::string prefix;
        std::string suffix;
        std::string find;
        std::string replacement;
    };
    /** @brief Render placeholders from locals first, then bindings, and apply matching tone rules. */
    std::string render(const std::string& text, const StateValue& bindings, const StateValue& locals,
                       const Evaluator& evaluate = {}) const;
    /** @brief Append a state-dependent character voice rule. */
    void addToneRule(std::string expression, std::string prefix, std::string suffix, std::string find,
                     std::string replacement);
    /** @brief Remove all character voice rules. */
    void clearToneRules() { rules_.clear(); }

private:
    std::vector<ToneRule> rules_;
};

}  // namespace eve::dialogue
