#pragma once
/** @file UIAutomationCapabilities.h
 * @brief ui module-interface: semantic automation and explicit observation of isolated authoring worlds.
 * @remarks No new tool family or persistent format is introduced; IUIAutomation owns semantic queries and input.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace ecs {
class Table;
}
namespace eve::ui {
/** @brief Registers the canonical UI automation provider. */
EVENGINE_API_WORLD void registerUIAutomationCapabilities();
/** @brief Scoped permission for canonical automation to inspect one independently owned UI world.
 * @ownership Observes a weak Table; queries temporarily pin it. Neither lease nor registry keeps a world alive.
 * @thread UI owner thread only; register/retire outside Views and automation queries. No callbacks or locks.
 * @remarks Current caller world remains visible. Duplicate leases deduplicate worlds; same widget names across worlds
 * remain ambiguous. Retiring the lease removes discovery; queued clicks still use ordinary host/tree validation.
 * World-first destruction makes discovery empty; lease-first destruction leaves the world and its hosts unchanged.
 */
class EVENGINE_API_WORLD UIAutomationWorld final {
public:
    /** @brief Release discovery only, without destroying hosts or writing data. */
    ~UIAutomationWorld();
    UIAutomationWorld(const UIAutomationWorld&)            = delete;
    UIAutomationWorld& operator=(const UIAutomationWorld&) = delete;
    /** @brief Observe an actual nonnull shared world without transferring its ownership.
     * @return Owning lease or InvalidArgument for a null world. Table/host state remains unchanged.
     * @cost One observation allocation and registry append per explicit attachment; none per rendered frame.
     */
    [[nodiscard]] static Result<std::unique_ptr<UIAutomationWorld>> observe(const std::shared_ptr<ecs::Table>& world);

private:
    struct Impl;
    explicit UIAutomationWorld(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
}  // namespace eve::ui
