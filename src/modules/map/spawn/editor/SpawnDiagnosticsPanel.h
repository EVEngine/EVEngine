#pragma once
/** @file SpawnDiagnosticsPanel.h
 * @brief spawn_editor runtime-observation face: owning, read-only published facts and visible refresh failures.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::spawn {
struct WorldDiagnostics;
}
namespace eve::ui {
class Component;
}
namespace eve::spawn_editor {
/** @brief UI owner of detached scheduler facts; never retains or calls a runtime world/provider.
 * @ownership Owns immutable captures and a standard UI component; rendered trees contain owning text only.
 * @thread UI owner thread. Capture on the simulation owner, then transfer its owning result here. No callbacks/locks.
 * @remarks This is captured scheduler state, not live domain health or an editable configuration/save participant.
 * Component builds copy row text and pinned template sources per dirty batch; coalesce refreshes.
 */
class EVENGINE_API_EDITORS SpawnDiagnosticsPanel final {
public:
    /** @brief Release UI/capture owners; no runtime state or subscription is changed. */
    ~SpawnDiagnosticsPanel();
    SpawnDiagnosticsPanel(const SpawnDiagnosticsPanel&)            = delete;
    SpawnDiagnosticsPanel& operator=(const SpawnDiagnosticsPanel&) = delete;
    /** @brief Bind one captured world/epoch/incarnation without runtime access.
     * @see spawn/SpawnDiagnostics.h for the complete capture type.
     * @return Owning panel or invalid binding identity; does not mount/initialize rendering.
     * @cost Owning snapshot transfer plus immutable presentation allocation per panel creation.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnDiagnosticsPanel>> create(spawn::WorldDiagnostics initial);
    /** @brief Publish an explicit capture result for the same binding at a nondecreasing tick/event cursor.
     * @return Success or refresh/conflict failure. Failure retains the last successful data and visibly marks it stale.
     * @ownership Consumes the result; copies no temporary runtime pointers. Recreate panel for a new world/epoch.
     * Allocation failure leaves the prior data owner intact; the host may retry the refresh.
     * @cost Owning capture transfer and presentation allocation per explicit refresh; no simulation work.
     */
    [[nodiscard]] Result<void> refresh(Result<spawn::WorldDiagnostics> capture);
    /** @brief Borrow the standard component until panel destruction; host uses normal attach/mount APIs. */
    [[nodiscard]] ui::Component& component() noexcept;
    /** @brief Share the actual immutable last successful capture, surviving refresh, panel and world destruction. */
    [[nodiscard]] std::shared_ptr<const spawn::WorldDiagnostics> snapshot() const noexcept;

private:
    struct Impl;
    explicit SpawnDiagnosticsPanel(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
