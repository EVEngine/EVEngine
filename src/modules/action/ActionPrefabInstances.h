#pragma once

/** @file ActionPrefabInstances.h @brief External ownership boundary for independent prefab spawns. */

#include "common/Result.h"
#include "common/RuntimeHandle.h"

#include <string>
#include <vector>

namespace eve::action {

/** @brief Owner tag preventing prefab instance handles from mixing with other registries. */
struct PrefabInstanceHandleTag {};

/** @brief Process-local, generation-qualified identity of one spawned prefab instance. */
using PrefabInstanceHandle = RuntimeHandle<PrefabInstanceHandleTag>;

/** @brief Owning inspection record for one externally managed prefab instance. */
struct PrefabInstanceInfo {
    PrefabInstanceHandle handle;
    std::string          uri;
};

/**
 * @brief Optional owner-thread service for prefab instances detached from an action block.
 *
 * The providing scene/presentation module remains the authoritative owner. The
 * caller receives handles and owning metadata only; no ECS or renderer pointer
 * crosses the module boundary.
 */
class IActionPrefabInstances {
public:
    static constexpr const char* capabilityName = "eve.action.prefab-instances";
    virtual ~IActionPrefabInstances() = default;

    /**
     * @brief List current independent instances in stable slot order.
     * @return Owning records; active action-owned and pooled instances are excluded.
     * @thread Owner/render thread only.
     */
    [[nodiscard]] virtual std::vector<PrefabInstanceInfo> independentInstances() const = 0;

    /**
     * @brief Recycle one independent instance and invalidate its handle.
     * @param handle Generation-qualified identity previously returned by independentInstances().
     * @return Applied, or NotFound for an invalid/stale/non-independent handle.
     * @thread Owner/render thread only; invokes no script callbacks.
     */
    [[nodiscard]] virtual Result<void> recycleIndependent(PrefabInstanceHandle handle) = 0;
};

}  // namespace eve::action
