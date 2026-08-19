#pragma once

#include "common/Export.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

struct EVENGINE_API ConsoleLine {
    std::string timestamp;  // HH:MM:SS
    std::string level;      // debug | info | warn | error | print | cmd | result
    std::string text;
};

/**
 * @brief In-engine runtime console / log ring buffer for DevTools.
 *
 * Provides a single place for script and engine messages: leveled log lines,
 * capture of Squirrel `print` / script errors, and a Squirrel REPL
 * (expression evaluation against the root table). Mirrors the AiPanel design —
 * thread-safe ring buffer, optional ImGui draw hook registered by the host so
 * EVDevTools does not need to include imgui.h.
 *
 * Script API (Debug builds, `eve run --debug`):
 *   eve.dev.console.log/info/warn/error/debug(text)
 *   eve.dev.console.eval(expr) -> result string
 *   eve.dev.console.clear() / recent(n) / format(n) / setVisible / toggleVisible
 *
 * Desktop-only (part of EVDevTools); not shipped on Android/iOS trimmed runtimes.
 */
class EVENGINE_API ConsolePanel {
public:
    static ConsolePanel& instance();

    ConsolePanel(const ConsolePanel&)            = delete;
    ConsolePanel& operator=(const ConsolePanel&) = delete;

    void setVisible(bool on);
    bool isVisible() const;
    void toggleVisible();

    /** @brief Append a leveled log line (thread-safe). */
    void addLog(std::string level, std::string text);
    void addInfo(std::string text);
    void addWarn(std::string text);
    void addError(std::string text);

    void clear();
    void setMaxEntries(size_t n);
    std::vector<ConsoleLine> recent(size_t max = 128) const;
    std::string format(size_t max = 128) const;

    /** @brief Attach to a Squirrel VM: capture `print`/script errors into the log. */
    void attach(HSQUIRRELVM vm);
    void detach();
    bool isAttached() const { return vm_ != nullptr; }

    /**
     * @brief Evaluate a Squirrel expression against the root table and return a
     * formatted result (or error message). Works as a runtime REPL.
     */
    std::string eval(const std::string& expression);

    /**
     * @brief Optional ImGui draw hook. Default no-op: console UI is registered by the
     * host (see setImGuiDrawer) so EVDevTools does not include imgui.h.
     */
    void drawImGui();

    using ImGuiDrawer = void (*)(ConsolePanel& panel);
    static void setImGuiDrawer(ImGuiDrawer fn);

private:
    // Immortal<ConsolePanel> constructs the singleton (devtools/Immortal.hpp).
    template <typename> friend struct Immortal;
    ConsolePanel() = default;

    static std::string nowStamp();

    mutable std::mutex mu_;
    bool               visible_    = true;
    size_t             maxEntries_ = 500;
    std::deque<ConsoleLine> log_;
    HSQUIRRELVM       vm_ = nullptr;
};

}  // namespace eve::dev
