#pragma once

#include "common/Export.h"
#include "devtools/CallGraph.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

enum class PauseReason : uint8_t {
    None = 0,
    PauseKey,
    Breakpoint,
    Step,
    Exception,
    Snapshot,
};

enum class RunMode : uint8_t {
    Running = 0,
    Paused,      // frame-level or script-level pause
    StepFrame,   // run one game frame then pause
    StepLine,    // run to next script line then pause
};

struct EVENGINE_API Breakpoint {
    std::string source;  // normalized path or basename
    int         line    = 0;
    bool        enabled = true;
    int         id      = 0;
};

struct EVENGINE_API WatchEntry {
    std::string expression;
    std::string value;  // last evaluated (display)
    bool        ok = false;
};

struct EVENGINE_API StackFrameInfo {
    int         id = 0;
    SourceLoc   loc;
    std::string name;
};

struct EVENGINE_API VariableInfo {
    std::string name;
    std::string value;
    std::string type;
    bool        expensive = false;
};

/**
 * Script + frame debugger: pause/step, breakpoints, watches.
 *
 * Frame pause: game loop skips eve_update (see load.nut + shouldRunUpdate).
 * Script pause: Squirrel line hook blocks until resume/step (waitWhilePaused).
 */
class EVENGINE_API Debugger {
public:
    static Debugger& instance();

    Debugger(const Debugger&)            = delete;
    Debugger& operator=(const Debugger&) = delete;

    void attach(HSQUIRRELVM vm);
    void detach();
    bool isAttached() const { return vm_ != nullptr; }
    HSQUIRRELVM vm() const { return vm_; }

    // ---- run control ----
    void     pause(PauseReason reason = PauseReason::PauseKey);
    void     resume();
    void     stepFrame();
    void     stepLine();
    bool     isPaused() const { return mode_.load() == RunMode::Paused; }
    RunMode  mode() const { return mode_.load(); }
    PauseReason lastPauseReason() const { return reason_.load(); }
    const SourceLoc& pauseLocation() const { return pauseLoc_; }

    /** Frame loop: true ⇒ call eve_update this frame. Consumes StepFrame. */
    bool shouldRunUpdate();
    /** After a frame when StepFrame was active → return to Paused. */
    void notifyFrameDone();

    /**
     * Called from Squirrel line debug hook.
     * Returns true if execution should block (breakpoint / step-line).
     */
    bool onScriptLine(const SourceLoc& loc);
    /** Block until resume/step/detach (processes external poll callbacks). */
    void waitWhilePaused(const std::function<void()>& pump = {});

    // ---- breakpoints ----
    int  setBreakpoint(std::string source, int line, bool enabled = true);
    bool clearBreakpoint(std::string source, int line);
    void clearBreakpoints(const std::string& source = {});
    std::vector<Breakpoint> breakpoints() const;
    bool hasBreakpoint(const std::string& source, int line) const;

    // ---- watches ----
    void addWatch(std::string expression);
    bool removeWatch(const std::string& expression);
    void clearWatches();
    std::vector<WatchEntry> watches() const;
    /** Re-evaluate all watches against current VM (paused preferred). */
    void refreshWatches();
    /** Evaluate a single expression (local name, or roottable slot). */
    VariableInfo evaluate(const std::string& expression) const;
    std::vector<VariableInfo> locals(int stackLevel = 1) const;
    std::vector<StackFrameInfo> stackTrace(int maxFrames = 32) const;

    using PumpFn = std::function<void()>;
    void setPump(PumpFn pump) { pump_ = std::move(pump); }

    /** Normalize source paths for breakpoint matching (basename fallback). */
    static std::string normalizeSource(std::string source);

private:
    Debugger() = default;

    bool matchBreakpoint(const std::string& source, int line) const;
    VariableInfo readLocal(HSQUIRRELVM vm, unsigned level, const std::string& name) const;
    VariableInfo readRoot(HSQUIRRELVM vm, const std::string& name) const;

    HSQUIRRELVM           vm_ = nullptr;
    std::atomic<RunMode>  mode_{RunMode::Running};
    std::atomic<PauseReason> reason_{PauseReason::None};
    SourceLoc             pauseLoc_;
    mutable std::mutex    mu_;
    std::vector<Breakpoint> bps_;
    std::vector<std::string> watchExprs_;
    std::vector<WatchEntry>  watchCache_;
    int                   nextBpId_ = 1;
    PumpFn                pump_;
    bool                  stepFrameArmed_ = false;
};

}  // namespace eve::dev
