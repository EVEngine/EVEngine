#pragma once
#include "common/Export.h"

#include "common/Module.h"

#include <memory>

namespace eve::animation_editor {

/** @brief Composition adapter that contributes animation editing commands and automation targets. */
class EVENGINE_API_EDITORS AnimationEditorModule final : public Module {
public:
    Module_REG(AnimationEditorModule);
    AnimationEditorModule();
    ~AnimationEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::animation_editor
