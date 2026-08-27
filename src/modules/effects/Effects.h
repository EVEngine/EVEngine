#pragma once

/**
 * @file Effects.h
 * @brief Public effects umbrella and script module facade.
 */

#include "common/Module.h"
#include "common/SquirrelOwnership.h"
#include "effects/EffectContainer.h"
#include "effects/EffectTypes.h"

#include <memory>
#include <vector>

namespace eve::effects {

/** @brief Owner tag for non-ECS effect-container handles. */
struct EffectContainerHandleTag {};
/** @brief Generation-qualified reference to a module-owned effect container. */
using EffectContainerHandleRef = eve::script::RuntimeHandleRef<EffectContainerHandleTag>;

/** @brief Script module factory for generic effect containers. */
class Effects : public Module {
public:
    Module_REG(Effects);
    Effects() = default;
    ~Effects() override = default;

    /**
     * @brief Allocates a container and returns its generation-qualified ownership reference.
     * @return A handle plus module-lifetime epoch; the Effects module owns the container.
     * @remarks The handle becomes stale after release, module unload, or a new
     *          Effects module instance. Callers never delete the resolved container.
     */
    [[nodiscard]] static eve::Result<EffectContainerHandleRef> newContainer();

    /** @brief Resolves a live container as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<EffectContainer> resolve(
        EffectContainerHandleRef reference) noexcept;

    /** @brief Releases a container owned by the Effects module. */
    [[nodiscard]] static eve::Result<void> release(EffectContainerHandleRef reference);

    /** @brief Reports whether a container reference is invalid for the current module. */
    [[nodiscard]] static bool isStale(EffectContainerHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<EffectContainer, EffectContainerHandleTag> containers_;
};

}  // namespace eve::effects
