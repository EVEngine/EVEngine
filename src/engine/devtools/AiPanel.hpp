#pragma once

#include "common/Export.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace eve::dev {

struct EVENGINE_API AiLogEntry {
    std::string timestamp;  // ISO-ish local time
    std::string kind;       // tool | note | system | error
    std::string title;
    std::string detail;
};

/**
 * In-engine AI / MCP session surface for DevTools.
 *
 * Records recent MCP tool calls and free-form notes so humans (ImGui panel /
 * script API) and agents (MCP resource `eve://ai-session`) share one log.
 * Drawing is optional — `drawImGui()` is a no-op when imgui is unavailable.
 */
class EVENGINE_API AiPanel {
public:
    static AiPanel& instance();

    AiPanel(const AiPanel&)            = delete;
    AiPanel& operator=(const AiPanel&) = delete;

    void setVisible(bool on);
    bool isVisible() const;
    void toggleVisible();

    void setMcpPort(int port);
    int  mcpPort() const;
    void setMcpConnected(bool on);
    bool mcpConnected() const;
    void setClientName(std::string name);
    std::string clientName() const;

    void clearLog();
    void addLog(std::string kind, std::string title, std::string detail = {});
    void addNote(std::string text);
    std::vector<AiLogEntry> recentLog(size_t max = 64) const;
    std::string formatLog(size_t max = 64) const;

    /** Compact status line for overlays / MCP resources. */
    std::string statusLine() const;

    /**
     * Optional ImGui draw hook. Default no-op: in-engine AI status is exposed
     * via MCP / `eve.dev.ai`. ImGui drawing is registered by the host so
     * EVDevTools does not need to include imgui.h.
     */
    void drawImGui();

    using ImGuiDrawer = void (*)(AiPanel& panel);
    static void setImGuiDrawer(ImGuiDrawer fn);

    void setMaxEntries(size_t n);
    size_t maxEntries() const { return maxEntries_; }

private:
    AiPanel() = default;

    static std::string nowStamp();

    mutable std::mutex mu_;
    bool               visible_      = true;
    int                mcpPort_      = 0;
    bool               mcpConnected_ = false;
    std::string        clientName_;
    size_t             maxEntries_   = 200;
    std::deque<AiLogEntry> log_;
};

}  // namespace eve::dev
