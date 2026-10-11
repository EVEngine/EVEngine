#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <map>
#include <memory>
#include <string>

namespace eve::scene_editing {
class SceneComponentPayloadRegistry;
}

namespace eve::scene_editor {

class ScriptComponentPayloadProvider;

/**
 * @brief Composition adapter that contributes scene editing commands and automation targets.
 *
 * 它还**持有**场景组件 payload 注册表并把它绑到每个自己创建的场景目标上（`bindComponentPayloads`）：
 * 注册表与"目标 id → 宿主名"的映射都只在这个模块里存在（目标 id 是自动化调用方选的，不等于宿主名），
 * 所以接线也归它。
 */
class EVENGINE_API_EDITORS SceneEditorModule final : public Module {
public:
    Module_REG(SceneEditorModule);
    /** @brief Scene editor module. */
    SceneEditorModule();
    /** @brief Scene editor module. */
    ~SceneEditorModule() override;

private:
    class TargetFactory;
    /** 自动化目标 id → 场景宿主名；只有创建过 `scene-host` 目标才知道。 */
    std::map<std::string, std::string>                            targetHosts_;
    std::unique_ptr<scene_editing::SceneComponentPayloadRegistry> payloads_;
    std::unique_ptr<ScriptComponentPayloadProvider>               scriptComponents_;
    std::unique_ptr<TargetFactory>                                factory_;
};

}  // namespace eve::scene_editor
