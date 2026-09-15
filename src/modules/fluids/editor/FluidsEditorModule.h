#pragma once

#include "common/Module.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <memory>

namespace eve::fluids_editor {

/**
 * @brief Creates schema-driven fluid documents for editor automation.
 * @ownership Returned targets own their authoring state; the factory retains nothing.
 * @thread Editor/composition-thread affine.
 * @reentrancy Does not invoke callbacks or retain request values.
 */
class FluidsAutomationTargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    /** @brief Report support for fluid-simulation, surface-fluid and volume-fluid documents. */
    bool supports(std::string_view type) const override;
    /** @brief Create a fluid document and optionally load its strict `snapshot` request field. */
    editor::EditorResult<editor::AutomationOwnedTarget> create(const editor::TargetId& target, std::string_view type,
                                                               const editor::EditorValue::Object& request) override;
};

/**
 * @brief Composition adapter that publishes the fluid automation-target factory.
 * @ownership Owns the registered factory and removes it before destruction.
 * @thread Owner/composition-thread affine.
 * @reentrancy Do not construct or destroy while factory listeners are being enumerated.
 */
class FluidsEditorModule final : public Module {
public:
    Module_REG(FluidsEditorModule);
    FluidsEditorModule();
    ~FluidsEditorModule() override;

private:
    std::unique_ptr<FluidsAutomationTargetFactory> factory_;
};

}  // namespace eve::fluids_editor
