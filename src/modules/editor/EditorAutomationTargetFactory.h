#pragma once

#include "editor/EditorResult.h"
#include "editor/EditorTarget.h"
#include "editor/EditorValue.h"

#include <memory>
#include <string_view>

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
    /** @brief Return whether this factory owns the requested stable target type. */
    [[nodiscard]] virtual bool supports(std::string_view type) const = 0;
    /**
     * @brief Create a domain target from an automation request.
     * @ownership Success transfers the target and any supporting lifetime to the caller.
     */
    [[nodiscard]] virtual EditorResult<AutomationOwnedTarget> create(
        const TargetId& target, std::string_view type, const EditorValue::Object& request) = 0;
};

}  // namespace eve::editor
