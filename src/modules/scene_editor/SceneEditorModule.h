#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::scene_editor {

/** @brief Composition adapter that contributes scene editing commands and automation targets. */
class SceneEditorModule final : public Module {
public:
    Module_REG(SceneEditorModule);
    SceneEditorModule();
    ~SceneEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::scene_editor
