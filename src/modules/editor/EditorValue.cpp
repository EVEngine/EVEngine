#include "editor/EditorValue.h"

#include <type_traits>

namespace eve::editor {

EditorValue::Type EditorValue::type() const { return static_cast<Type>(storage_.index()); }

namespace {

struct ValueBudget {
    size_t elements    = 0;
    size_t stringBytes = 0;
};

bool checkValue(const EditorValue& value, size_t depth, size_t maxDepth, size_t maxElements, size_t maxStringBytes,
                ValueBudget& budget) {
    if (depth > maxDepth) return false;
    return std::visit(
        [&](const auto& current) -> bool {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::string>) {
                budget.stringBytes += current.size();
                return budget.stringBytes <= maxStringBytes;
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                budget.elements += current.size();
                if (budget.elements > maxElements) return false;
                for (const auto& entry : current)
                    if (!checkValue(entry, depth + 1, maxDepth, maxElements, maxStringBytes, budget)) return false;
                return true;
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                budget.elements += current.size();
                if (budget.elements > maxElements) return false;
                for (const auto& [key, entry] : current) {
                    budget.stringBytes += key.size();
                    if (budget.stringBytes > maxStringBytes ||
                        !checkValue(entry, depth + 1, maxDepth, maxElements, maxStringBytes, budget))
                        return false;
                }
                return true;
            } else {
                return true;
            }
        },
        value.storage());
}

}  // namespace

bool EditorValue::withinLimits(size_t maxDepth, size_t maxElements, size_t maxStringBytes) const {
    if (maxDepth == 0) return false;
    ValueBudget budget;
    return checkValue(*this, 1, maxDepth, maxElements, maxStringBytes, budget);
}

}  // namespace eve::editor
