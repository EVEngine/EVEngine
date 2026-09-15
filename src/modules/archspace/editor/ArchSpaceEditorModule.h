#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::archspace_editor {

/**
 * @brief Composition adapter that contributes ArchSpace editing commands and automation targets.
 *
 * @ownership The automation factory is owned by this module. Registered commands are owned by the
 *            borrowed editing host and unregistered on destruction.
 * @threadaffinity Owner/composition thread only.
 * @reentrancy Do not construct or destroy while a command planner is running.
 */
class ArchSpaceEditorModule final : public Module {
public:
    Module_REG(ArchSpaceEditorModule);
    ArchSpaceEditorModule();
    ~ArchSpaceEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::archspace_editor
