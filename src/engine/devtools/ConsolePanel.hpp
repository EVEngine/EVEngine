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

struct EVENGINE_API_FOUNDATION_INLINE ConsoleLine {
    std::uint64_t seq = 0;    // process-lifetime sequence number; never reused
    std::string   timestamp;  // HH:MM:SS
    std::string   level;      // debug | info | warn | error | print | cmd | result | engine
    std::string   text;
};

/**
 * @brief One bounded read of the console ring buffer together with its cursors.
 *
 * Sequence numbers are assigned monotonically for the whole process, so a reader
 * can hold a cursor and never re-read a line. Lines disappear when the ring
 * overflows or when the buffer is cleared; `droppedThrough` reports how far the
 * retained window has advanced, so a caller can tell "nothing new happened" from
 * "lines were dropped before I read them".
 */
struct EVENGINE_API_FOUNDATION_INLINE ConsoleSlice {
    std::vector<ConsoleLine> lines;
    std::uint64_t            firstSeq       = 0;  // oldest retained seq (nextSeq when empty)
    std::uint64_t            nextSeq        = 0;  // seq the next appended line will receive
    std::uint64_t            droppedThrough = 0;  // highest seq no longer retained
    /** Highest seq covered by this read; pass it back as the next `afterSeq`. */
    std::uint64_t cursor    = 0;
    bool          truncated = false;  // cursor predates the retained window
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
class EVENGINE_API_FOUNDATION ConsolePanel {
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
    /**
     * @brief Append one line captured from process stderr under level `engine`.
     *
     * A line identical to the immediately preceding script-captured entry
     * (`print` / `error`) is dropped: `capturePrint`/`captureError` forward to
     * stderr, so the descriptor-level capture would otherwise duplicate every
     * script message. Genuine consecutive engine lines are kept.
     * @param text One complete line, without its newline.
     */
    void addEngineLine(const std::string& text);
    /** @brief Whether process stderr is currently mirrored into this console. */
    [[nodiscard]] bool isCapturingStderr() const;

    void                     clear();
    void                     setMaxEntries(size_t n);
    std::vector<ConsoleLine> recent(size_t max = 128) const;
    std::string              format(size_t max = 128) const;

    /** @brief Sequence number the next appended line will receive. */
    std::uint64_t nextSeq() const;
    /** @brief Highest sequence number no longer retained (evicted or cleared). */
    std::uint64_t droppedThrough() const;
    /**
     * @brief Read retained lines relative to a sequence cursor.
     * @param afterSeq Return lines with `seq > afterSeq`; 0 selects the newest lines.
     * @param max Upper bound on returned lines; 0 selects 64.
     * @param level Optional exact level filter; empty keeps every level.
     * @return Slice plus the cursors needed for the next incremental read.
     * @thread Any thread; the ring buffer is mutex-guarded.
     */
    [[nodiscard]] ConsoleSlice read(std::uint64_t afterSeq, size_t max = 64, const std::string& level = {}) const;

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
    template <typename>
    friend struct Immortal;
    ConsolePanel() = default;

    static std::string nowStamp();

    /** Append with `mu_` already held (shared by addLog and addEngineLine). */
    void appendLocked(std::string level, std::string text);

    mutable std::mutex      mu_;
    bool                    visible_    = true;
    size_t                  maxEntries_ = 500;
    std::deque<ConsoleLine> log_;
    HSQUIRRELVM             vm_ = nullptr;
    // Monotonic for the process lifetime: clear() drops lines but never rewinds
    // these, so an existing reader cursor stays meaningful.
    std::uint64_t nextSeq_        = 1;
    std::uint64_t droppedThrough_ = 0;
};

}  // namespace eve::dev
