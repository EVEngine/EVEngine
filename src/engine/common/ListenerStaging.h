#pragma once
/** @file ListenerStaging.h
 * @brief common module-interface: synchronous owning-builder staging of selected capability listener additions.
 * @remarks Services and withdrawals of existing listeners retain the ordinary capability protocol.
 */
#include <memory>
#include <span>
#include <string_view>
#include "common/Result.h"
namespace eve::cap {
class ListenerStaging;
namespace detail {
struct ListenerStageAccess;
}
/** @brief Temporarily route external callbacks through the ordinary registry rather than the enclosing builder.
 * @ownership Borrows a stationary staging scope, which must outlive this guard; no use across frames/tasks.
 * @thread Registration owner thread only; callbacks must finish any scopes they open before returning.
 */
class EVENGINE_API_FOUNDATION ListenerStagingPause final {
public:
    /** @brief Resume capture when this active guard leaves its synchronous scope. */
    ~ListenerStagingPause();
    /** @brief Transfer the resume responsibility. */
    ListenerStagingPause(ListenerStagingPause&&) noexcept;
    ListenerStagingPause& operator=(ListenerStagingPause&&)      = delete;
    ListenerStagingPause(const ListenerStagingPause&)            = delete;
    ListenerStagingPause& operator=(const ListenerStagingPause&) = delete;

private:
    friend class ListenerStaging;
    explicit ListenerStagingPause(ListenerStaging& owner) noexcept;
    ListenerStaging* owner_;
};
/** @brief Capture listener additions during one synchronous isolated-owner construction operation.
 * @ownership Providers remain caller-owned. They must stay alive until publication or withdraw normally.
 * Cancellation discards pending addresses without dereferencing them, including failed constructor prefixes.
 * @thread Same registration-owner affinity as Capability.h; no concurrent registry mutation/dispatch.
 * Do not retain this active scope across frames, yields or tasks. Captured families are owning copied names.
 */
class EVENGINE_API_FOUNDATION ListenerStaging final {
public:
    /** @brief Cancel unpublished additions and leave capture; invokes no provider callbacks. */
    ~ListenerStaging();
    ListenerStaging(const ListenerStaging&)            = delete;
    ListenerStaging& operator=(const ListenerStaging&) = delete;
    ListenerStaging(ListenerStaging&&)                 = delete;
    ListenerStaging& operator=(ListenerStaging&&)      = delete;
    /** @brief Begin capture for 1..64 distinct nonempty listener families, leaving live queries unchanged.
     * @param families Listener capability names copied into this bounded construction scope.
     * @return Stable scope owner or invalid names/nested active scope/registry shutdown failure.
     * @ownership Names are copied; a scope begun by an external callback must end before resuming its caller.
     * @cost Copies bounded family names and allocates local staging once per owning builder.
     * @throws std::bad_alloc No registrations or capture context changed.
     */
    [[nodiscard]] static Result<std::unique_ptr<ListenerStaging>> begin(std::span<const std::string_view> families);
    /** @brief Suspend this active scope for a synchronous external callback, resuming on guard destruction.
     * @return Resume guard or inactive/published/already-suspended scope failure; no callbacks execute here.
     */
    [[nodiscard]] Result<ListenerStagingPause> pause();
    /** @brief Prepare and atomically publish all additions, preserving priority, tie order and idempotency.
     * @return Applied or inactive/already-published/registry-reset failure. Reset requires a new scope.
     * @remarks Copies the current registry before one no-throw exchange. No callbacks or provider destruction.
     * @cost Copies process-wide capability metadata once per explicit owning-world publication.
     * @throws std::bad_alloc Live registrations remain unchanged; pending additions remain available for retry.
     */
    [[nodiscard]] Result<void> publish();

private:
    friend class ListenerStagingPause;
    friend struct detail::ListenerStageAccess;
    struct Impl;
    explicit ListenerStaging(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::cap
