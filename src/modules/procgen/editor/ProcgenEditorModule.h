#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::procgen_editor {

/**
 * @brief Composition adapter for the script-hosted procgen generator editor.
 *
 * @ownership The automation factory is owned by this module.
 * @threadaffinity Owner/composition thread only.
 * @reentrancy Do not construct or destroy while a command planner is running.
 */
class ProcgenEditorModule final : public Module {
public:
    Module_REG(ProcgenEditorModule);
    ProcgenEditorModule();
    ~ProcgenEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::procgen_editor
