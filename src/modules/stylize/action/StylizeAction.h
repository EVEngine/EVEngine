#pragma once
#include "common/Export.h"

/**
 * @file StylizeAction.h
 * @brief Action/camera adapter satellite for AttackVfx presentation blocks.
 *
 * Owns a shared AttackVfxRuntime used by presentation:attack-vfx* handlers and
 * registers the camera IAttackVfxLayerExecutor. Stylize itself does not depend
 * on action; this LAYER 5 satellite closes that edge.
 */

#include "common/Module.h"
#include "common/Result.h"
#include "stylize/AttackVfxRuntime.h"

#include <string>
#include <string_view>

namespace eve::stylize_action {

/**
 * @brief Composition entry for AttackVfx Action notify handlers and camera layers.
 *
 * @ownership Owns the module-scoped AttackVfxRuntime pool. Layer GPU/audio
 *            resources remain with registered IAttackVfxLayerExecutor providers.
 * @thread Owner-thread only.
 */
class EVENGINE_API_DOMAINS StylizeAction final : public eve::Module {
public:
    Module_REG(StylizeAction);

    StylizeAction();
    ~StylizeAction() override;

    /** @brief Borrowed module-owned runtime used by Action handlers and tests. */
    [[nodiscard]] eve::stylize::AttackVfxRuntime& runtime() noexcept { return runtime_; }
    [[nodiscard]] const eve::stylize::AttackVfxRuntime& runtime() const noexcept { return runtime_; }

    /**
     * @brief Validate and register a JSON recipe document on the module runtime.
     * @param json UTF-8 AttackVfxRecipe document.
     */
    [[nodiscard]] eve::Result<void> registerRecipeJson(std::string_view json);

    /**
     * @brief Validate and register a JSON skin document on the module runtime.
     * @param json UTF-8 AttackVfxSkin document.
     */
    [[nodiscard]] eve::Result<void> registerSkinJson(std::string_view json);

private:
    eve::stylize::AttackVfxRuntime runtime_;
};

}  // namespace eve::stylize_action
