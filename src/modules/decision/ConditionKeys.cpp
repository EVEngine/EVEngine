#include "decision/ConditionKeys.h"

namespace eve::decision {

std::optional<std::string> authorityAllowScope(const Value& arguments) {
    const Value* scopeValue = arguments.find(std::string("scope"));
    const auto*  scopeText  = scopeValue == nullptr ? nullptr : scopeValue->getIf<std::string>();
    if (scopeText == nullptr || scopeText->empty()) return std::nullopt;
    return *scopeText;
}

}  // namespace eve::decision
