#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <memory>

namespace eve::scene_editor {

/** @brief Composition adapter that contributes scene editing commands and automation targets. */
class EVENGINE_API_EDITORS SceneEditorModule final : public Module {
public:
    Module_REG(SceneEditorModule);
    /** @brief Scene editor module. */
    SceneEditorModule();
    /** @brief Scene editor module. */
    ~SceneEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::scene_editor
