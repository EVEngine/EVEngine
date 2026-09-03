#pragma once

#include "common/Export.h"
#include "devtools/CallGraph.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
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
    /** @brief Script state was restored while the debugger was already paused. */
    Snapshot,
};

enum class RunMode : uint8_t {
    Running = 0,
    Paused,      // frame-level or script-level pause
    StepFrame,   // run one game frame then pause
    StepInto,    // next script line (any call depth) — DAP stepIn
    StepOver,    // next script line at ≤ start depth — DAP next
    StepOut,     // next script line at < start depth — DAP stepOut
};

struct EVENGINE_API Breakpoint {
    std::string source;  // normalized path or basename
    int         line    = 0;
    bool        enabled = true;
    bool        verified = false;
    std::string condition;  // optional GDScript-style condition; empty = always
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
    bool        expandable = false;  // container / instance: children available
    int         childCount = -1;     // -1 = unknown
};

/** @brief Where a variable tree node is rooted (used by containerChildren). */
enum class VarKind : uint8_t {
    Locals   = 0,  // frame locals
    Globals  = 1,  // roottable
    // Closure upvalues are children of a closure value, not a third root.
};

/**
 * @brief Script + frame debugger: pause/step, breakpoints, watches.
 *
 * Frame pause: game loop skips eve_update (see load.nut + shouldRunUpdate).
 * Script pause: Squirrel line hook blocks until resume/step (waitWhilePaused).
 *
 * Stepping (script, when stopped on a line):
 *  - stepInto  — stop on the next line event (enter calls)
 *  - stepOver  — stop on the next line at the same / outer stack depth
 *  - stepOut   — stop after returning to the caller
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
    /**
     * @brief Frame-level pause without a script site (PauseKey clears pauseLocation).
     * For Exception / Snapshot, prefer pauseAt so DAP/MCP keep a source location.
     * @param reason Pause cause stored in lastPauseReason().
     */
    void     pause(PauseReason reason = PauseReason::PauseKey);
    /**
     * @brief Pause and record the script site (copied; Observed until the next
     * pause/resume). Thread: VM / game loop that owns waitWhilePaused.
     * @param reason Pause cause (Exception, Snapshot, Breakpoint, …).
     * @param loc Source location shown to DAP/MCP; empty clears pauseLocation.
     */
    void     pauseAt(PauseReason reason, SourceLoc loc);
    void     resume();
    void     stepFrame();
    /** @brief Enter calls: stop on the next script line at any depth. */
    void     stepInto();
    /** @brief Skip calls: stop on the next line at ≤ current stack depth. */
    void     stepOver();
    /** @brief Finish current function: stop when stack depth drops. */
    void     stepOut();
    /** @brief Alias for stepInto (historical name). */
    void     stepLine() { stepInto(); }
    /**
     * @brief Convenience: script stepOver when mid-hook; otherwise one game frame.
     * Prefer stepInto/stepOver/stepOut from DAP / UI.
     */
    void     step();
    bool     isPaused() const { return mode_.load() == RunMode::Paused; }
    RunMode  mode() const { return mode_.load(); }
    PauseReason lastPauseReason() const { return reason_.load(); }
    const SourceLoc& pauseLocation() const { return pauseLoc_; }
    /** @brief Current Squirrel call depth (1 = topmost script frame). 0 if none. */
    int      scriptStackDepth() const;

    /** @brief Frame loop: true ⇒ call eve_update this frame. Consumes StepFrame. */
    bool shouldRunUpdate();
    /** @brief After a frame when StepFrame was active → return to Paused. */
    void notifyFrameDone();

    /**
     * @brief Use DevTool's CallGraph for stack names (same object the slicer sees).
     * Pass nullptr to record calls on the debugger's own graph (unit tests).
     * @param graph Shared CallGraph, or nullptr for the debugger-owned graph.
     */
    void setCallGraph(CallGraph* graph);
    /**
     * @brief In-memory script text used to name anonymous `name = function()` bindings
     * when the hook source is a compile buffer, not a file.
     * @param source Compile name / path as reported by the Squirrel debug hook.
     * @param text Full source of that buffer.
     */
    void setVirtualSource(std::string source, std::string text);

    /**
     * @brief Squirrel call debug hook ('c'). Records a frame on the active CallGraph.
     * @param loc Hook location (function name may be `unknown` for anonymous closures).
     * @return Display name stored on the new frame (binding name when the closure is anonymous).
     */
    std::string onCall(SourceLoc loc);
    /**
     * @brief Squirrel return debug hook ('r'). Pops the active CallGraph frame.
     * @param loc Hook location of the returning function.
     */
    void onReturn(const SourceLoc& loc);

    /**
     * @brief Called from Squirrel line debug hook.
     * Returns true if execution should block (breakpoint / step).
     */
    bool onScriptLine(const SourceLoc& loc);
    /** @brief Block until resume/step/detach (processes external poll callbacks). */
    void waitWhilePaused(const std::function<void()>& pump = {});

    // ---- breakpoints ----
    /**
     * @brief Add or update a line breakpoint. A later call with the same source+line
     * replaces `enabled` and `condition` (empty condition clears a previous one).
     * @return Breakpoint id, or 0 if source is empty / line is not positive.
     */
    int  setBreakpoint(std::string source, int line, bool enabled = true,
                       std::string condition = {});
    bool clearBreakpoint(std::string source, int line);
    void clearBreakpoints(const std::string& source = {});
    /** @brief Enable or disable by id. @return false if id is unknown. */
    bool setBreakpointEnabled(int id, bool enabled);
    std::vector<Breakpoint> breakpoints() const;
    /**
     * @brief True if a breakpoint is registered at source+line.
     * Ignores the skip-all master switch and the per-breakpoint enabled flag.
     */
    bool hasBreakpoint(const std::string& source, int line) const;

    // ---- watches ----
    void addWatch(std::string expression);
    bool removeWatch(const std::string& expression);
    void clearWatches();
    std::vector<WatchEntry> watches() const;
    /** @brief Re-evaluate all watches against current VM (paused preferred). */
    void refreshWatches();
    /**
     * @brief Evaluate an expression in the given frame's scope.
     * Understands plain names, `a.b` paths, and full Squirrel expressions
     * (arithmetic / calls / indexing) compiled against a locals+roottable env.
     */
    VariableInfo evaluate(const std::string& expression, int frameLevel = 0) const;
    /** @brief Locals of the given call-stack level (0 = current script frame). */
    std::vector<VariableInfo> locals(int stackLevel = 0) const;
    /** @brief Root-table slots (globals). */
    std::vector<VariableInfo> globals() const;
    /**
     * @brief Children of a container variable. `path` is the key chain that locates
     * the container from its root (locals frame or roottable).
     * Empty `path` lists the root entries themselves.
     */
    std::vector<VariableInfo> containerChildren(VarKind kind, int frame,
                                                const std::vector<std::string>& path) const;
    std::vector<StackFrameInfo> stackTrace(int maxFrames = 32) const;

    // ---- error / breakpoint policy ----
    /** @brief Break on script errors (Godot "Break on Error"). Default off. */
    void setBreakOnError(bool on) { breakOnError_.store(on); }
    bool breakOnError() const { return breakOnError_.load(); }
    /** @brief Master switch for all breakpoints ("skip all breakpoints"). */
    void setBreakpointsEnabled(bool on) { bpsEnabled_.store(on); }
    bool breakpointsEnabled() const { return bpsEnabled_.load(); }
    /** @brief Called once when a breakpoint line is first observed by the line hook. */
    using BreakpointEventFn = std::function<void(int id, const std::string& source, int line,
                                                 bool verified)>;
    void setBreakpointEventFn(BreakpointEventFn fn) { bpEventFn_ = std::move(fn); }

    using PumpFn = std::function<void()>;
    void setPump(PumpFn pump) { pump_ = std::move(pump); }

    /** @brief Normalize source paths for breakpoint matching (basename fallback). */
    static std::string normalizeSource(std::string source);
    /** @brief Basename of a normalized path (empty-safe). */
    static std::string sourceBasename(const std::string& source);
    /**
     * @brief True when two source paths refer to the same script file.
     * Matches exact path, or one path as a directory-bounded suffix of the other.
     * A bare filename (no `/`) also matches any path with the same basename.
     * Two directory-qualified paths that only share a basename do not match.
     */
    static bool sourcesMatch(const std::string& a, const std::string& b);

private:
    Debugger() = default;

    bool matchBreakpoint(const std::string& source, int line) const;
    bool matchBreakpointAny(const std::string& source, int line) const;
    bool conditionHolds(const Breakpoint& bp) const;
    bool pushPathValue(HSQUIRRELVM vm, VarKind kind, int frame,
                       const std::vector<std::string>& path) const;
    VariableInfo readLocal(HSQUIRRELVM vm, unsigned level, const std::string& name) const;
    VariableInfo readRoot(HSQUIRRELVM vm, const std::string& name) const;
    VariableInfo evaluateLegacy(const std::string& expression) const;
    bool pushLocalValue(HSQUIRRELVM vm, unsigned level, const std::string& name) const;
    void beginScriptStep(RunMode mode);
    CallGraph& activeGraph();
    const CallGraph& activeGraph() const;
    std::string resolveCallName(const SourceLoc& loc) const;
    std::string lineAt(const std::string& source, int line) const;
    std::string nameFromPrototype(int level, const char* rawName, const SourceLoc& loc) const;
    void applyCallGraphNames(std::vector<StackFrameInfo>& frames) const;

    HSQUIRRELVM           vm_ = nullptr;
    std::atomic<RunMode>  mode_{RunMode::Running};
    std::atomic<PauseReason> reason_{PauseReason::None};
    std::atomic<bool>     breakOnError_{false};
    std::atomic<bool>     bpsEnabled_{true};
    SourceLoc             pauseLoc_;
    mutable std::mutex    mu_;
    std::vector<Breakpoint> bps_;
    std::vector<std::string> watchExprs_;
    std::vector<WatchEntry>  watchCache_;
    int                   nextBpId_ = 1;
    PumpFn                pump_;
    bool                  stepFrameArmed_ = false;
    int                   stepStartDepth_ = 0;
    /** @brief While set, step filters ignore this exact source+line (multi-_OP_LINE). */
    SourceLoc             stepSkipLoc_;
    BreakpointEventFn     bpEventFn_;
    CallGraph*            callGraph_ = nullptr;
    CallGraph             ownGraph_;
    std::unordered_map<std::string, std::string> virtualSources_;
};

}  // namespace eve::dev
