#pragma once
#include "common/Export.h"

/**
 * @file AttackVfxLayerExecutor.h
 * @brief Pluggable AttackVfx layer backends registered via capability listeners.
 *
 * Stylize owns the executor interface. Provider modules (particles, stylize_action
 * camera bridge, later MeshVFX/decal) register with eve::cap::addListener and
 * must not reverse-include each other through stylize.
 */

#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"

#include <cstddef>
#include <cstdint>

namespace eve::stylize {

/** @brief Opaque executor-owned layer instance identity. */
struct AttackVfxLayerHandle {
    std::uint64_t id = 0;

    friend bool operator==(const AttackVfxLayerHandle&, const AttackVfxLayerHandle&) = default;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

/**
 * @brief Borrowed start context for one layer inside an entering phase.
 *
 * Pointers are valid only for the synchronous start/update/stop call.
 * Executors must copy any data they need to retain.
 */
struct AttackVfxLayerStartRequest {
    AttackVfxHandle          instance{};
    AttackVfxPhaseKind       phase      = AttackVfxPhaseKind::Release;
    std::size_t              phaseIndex = 0;
    std::size_t              layerIndex = 0;
    const AttackVfxLayer*    layer      = nullptr;
    const AttackVfxSkin*     skin       = nullptr;
    const AttackVfxRequest*  playRequest = nullptr;
};

/**
 * @brief Optional open backend for one AttackVfxLayerRole.
 *
 * @thread Owner-thread only. Calls are synchronous; do not retain borrowed
 *         request pointers across calls.
 * @reentrancy Must not reenter AttackVfxRuntime.
 */
class IAttackVfxLayerExecutor {
public:
    static constexpr const char* capabilityName = "eve.stylize.attack-vfx-layer-executor";
    virtual ~IAttackVfxLayerExecutor() = default;

    /** @brief Role this executor owns. One listener should own one role. */
    [[nodiscard]] virtual AttackVfxLayerRole role() const noexcept = 0;

    /**
     * @brief Start one layer for an entering phase.
     * @return Opaque handle owned by the executor until stop().
     */
    [[nodiscard]] virtual Result<AttackVfxLayerHandle> start(const AttackVfxLayerStartRequest& request) = 0;

    /**
     * @brief Advance one live layer with injected simulation seconds.
     * @param request Borrowed context for the same phase/layer indices.
     */
    [[nodiscard]] virtual Result<void> update(AttackVfxLayerHandle handle, double dtSeconds,
                                              const AttackVfxLayerStartRequest& request) = 0;

    /** @brief Tear down one layer using the authored stop behavior. */
    [[nodiscard]] virtual Result<void> stop(AttackVfxLayerHandle handle, AttackVfxStopBehavior behavior) = 0;
};

}  // namespace eve::stylize
