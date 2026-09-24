#pragma once

#include "editor/EditorResult.h"
#include "editor/EditorTarget.h"
#include "editor/EditorValue.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

namespace eve::editor {

/** @brief Target plus optional borrowed-dependency owner returned by an automation factory. */
struct AutomationOwnedTarget {
    // Declared first so reverse destruction destroys the borrowing target before its support object.
    std::shared_ptr<void>              support;
    std::unique_ptr<IEditableTarget> target;
};

/** @brief Multi-provider extension point for domain-specific automation target creation. */
class IEditorAutomationTargetFactory {
public:
    static constexpr const char* capabilityName = "IEditorAutomationTargetFactory";

    virtual ~IEditorAutomationTargetFactory() = default;

    /**
     * @brief Stable target type names this factory accepts.
     *
     * Discovery is the reason this is a list rather than only a predicate: an
     * agent could not previously learn which `type` values exist, so the built-in
     * schema had to hard-code a subset that drifted from the loaded providers.
     * @return Values returned by value on purpose: a returned
     *         `std::initializer_list` would dangle, because its backing array has
     *         the lifetime of the returned object's full-expression.
     */
    [[nodiscard]] virtual std::vector<std::string_view> types() const = 0;

    /** @brief Whether this factory owns the requested stable target type. */
    [[nodiscard]] bool supports(std::string_view type) const {
        const std::vector<std::string_view> accepted = types();
        return std::find(accepted.begin(), accepted.end(), type) != accepted.end();
    }

    /**
     * @brief Create a domain target from an automation request.
     * @ownership Success transfers the target and any supporting lifetime to the caller.
     */
    [[nodiscard]] virtual EditorResult<AutomationOwnedTarget> create(
        const TargetId& target, std::string_view type, const EditorValue::Object& request) = 0;
};

}  // namespace eve::editor
