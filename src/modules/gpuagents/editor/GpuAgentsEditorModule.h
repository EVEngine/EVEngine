#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <memory>
#include <string_view>
#include <vector>

namespace eve::gpuagents_editor {

/**
 * @brief Creates schema-driven GPU Agents documents for editor automation.
 * @ownership Returned targets own their authoring state; the factory retains nothing.
 * @thread Editor/composition-thread affine.
 * @reentrancy Does not invoke callbacks or retain request values.
 */
class EVENGINE_API_EDITORS GpuAgentsAutomationTargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    /** @brief Accepted GPU Agents target type names. */
    std::vector<std::string_view> types() const override;
    /** @brief Create a GPU Agents document and optionally load its `snapshot` request field. */
    editor::Result<editor::AutomationOwnedTarget> create(const editor::TargetId& target, std::string_view type,
                                                               const editor::EditorValue::Object& request) override;
};

/**
 * @brief Composition adapter that publishes the GPU Agents automation-target factory.
 * @ownership Owns the registered factory and removes it before destruction.
 * @thread Owner/composition-thread affine.
 * @reentrancy Do not construct or destroy while factory listeners are being enumerated.
 */
class EVENGINE_API_EDITORS GpuAgentsEditorModule final : public Module {
public:
    Module_REG(GpuAgentsEditorModule);
    GpuAgentsEditorModule();
    ~GpuAgentsEditorModule() override;

private:
    std::unique_ptr<GpuAgentsAutomationTargetFactory> factory_;
};

}  // namespace eve::gpuagents_editor
