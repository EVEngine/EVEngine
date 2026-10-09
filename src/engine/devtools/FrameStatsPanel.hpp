#pragma once

#include "common/Export.h"

#include <mutex>
#include <string>

namespace eve::dev {

/**
 * @brief Lightweight FPS / frame-time overlay for DevTools (0.6 closeout).
 *
 * Call `sample(dtSeconds)` once per frame (typically from `load.nut` when
 * `--debug` is on). Optional `setEntityCount` lets games expose ECS size.
 * ImGui drawing is registered by the host (`ImGuiHostPanels.cpp`) so EVDevTools
 * does not include imgui.h.
 *
 * Script API (`eve run --debug`):
 *   eve.dev.stats.sample(dt) / setEntityCount(n) / clearEntityCount()
 *   eve.dev.stats.fps() / frameMs() / entityCount() / format()
 *   eve.dev.stats.setVisible / isVisible / toggleVisible / draw
 */
class EVENGINE_API_FOUNDATION FrameStatsPanel {
public:
    static FrameStatsPanel& instance();

    FrameStatsPanel(const FrameStatsPanel&)            = delete;
    FrameStatsPanel& operator=(const FrameStatsPanel&) = delete;

    void setVisible(bool on);
    bool isVisible() const;
    void toggleVisible();

    /**
     * @brief Feed one frame duration in seconds (must be finite and > 0).
     * Updates EMA FPS and last frame milliseconds.
     */
    void sample(float dtSeconds);

    /** @brief Optional entity / actor count shown in the overlay; negative clears. */
    void setEntityCount(int count);
    void clearEntityCount();

    float fps() const;
    float frameMs() const;
    int   entityCount() const;  ///< -1 when unset
    bool  hasEntityCount() const;

    /** @brief One-line summary for logs / scripts, e.g. `60.1 fps | 16.6 ms | 128 entities`. */
    std::string format() const;

    void drawImGui();

    using ImGuiDrawer = void (*)(FrameStatsPanel& panel);
    static void setImGuiDrawer(ImGuiDrawer fn);

private:
    template <typename>
    friend struct Immortal;
    FrameStatsPanel() = default;

    mutable std::mutex mu_;
    bool               visible_         = false;
    float              lastDt_          = 0.f;
    float              emaFps_          = 0.f;
    int                entityCount_     = -1;
};

}  // namespace eve::dev
