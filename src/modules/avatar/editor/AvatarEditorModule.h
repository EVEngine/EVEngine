#pragma once
#include "common/Export.h"

#include "common/Module.h"

#include <memory>

namespace eve::avatar_editor {

/** @brief Composition adapter that contributes avatar editing commands and automation targets. */
class EVENGINE_API_EDITORS AvatarEditorModule final : public Module {
public:
    Module_REG(AvatarEditorModule);
    AvatarEditorModule();
    ~AvatarEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::avatar_editor
