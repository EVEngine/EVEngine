#pragma once

#include "common/Export.h"
#include "devtools/CallGraph.hpp"
#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/RenderFlow.hpp"
#include "devtools/ScenarioRecorder.h"
#include "devtools/Snapshot.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace ssq {
class VM;
}

namespace eve {
class Runtime;
}

namespace eve::dev {

class McpServer;
class AiPanel;
class ConsolePanel;

/**
 * @brief Platform-level script + render debugger / dynamic slicer front-end.
 *
 * When attached (typically via `eve run --debug`):
 *  - Squirrel: debug hook → CallGraph (call stack + Def/Use data-flow)
 *  - Breakpoints / watches / pause-step via Debugger
 *  - Optional DAP TCP server for the VS Code eve-debug extension
 *  - Optional MCP TCP server for AI agents (tools/resources/prompts)
 *  - EveScript LanguageServer (stdio LSP via `eve language-server`, typed API for tests)
 *  - Script-state Snapshot (engine treated as stateless)
 *  - Render: installs RenderFlow as eve::debug::IRenderTracer
 *  - on errors: Weiser-style backward slice for script and/or render pipeline
 *  - AI panel: session log + optional ImGui "AI / MCP" window
 *
 * Not shipped on Android/iOS trimmed runtimes (EVDevTools is desktop-only).
 */
class EVENGINE_API DevTool {
public:
    static DevTool& instance();

    DevTool(const DevTool&)            = delete;
    DevTool& operator=(const DevTool&) = delete;

    /** @brief Attach script tracer to VM and enable render tracing. */
    void attach(ssq::VM& vm, bool sampleLocals = true);
    void attach(HSQUIRRELVM vm, bool sampleLocals = true);
    /**
     * @brief Attach to a Runtime and route its boundary errors into the slicer.
     * @param runtime     Runtime whose errors (compile/reflect/unload, plus any
     *                    uncaught error the VM hook did not already report) are
     *                    forwarded to notifyError().
     * @param sampleLocals Whether to sample frame locals for data-flow slicing.
     */
    void attach(eve::Runtime& runtime, bool sampleLocals = true);
    /** @brief Enable render-flow tracing without a Squirrel VM (C++ / unit tests). */
    void enableRenderTrace(bool on = true);
    void detach();

    /** @brief Expose `eve.dev` script API (pause/breakpoint/watch/snapshot/AI). */
    void exposeScriptApi(ssq::VM& vm);

    /** @brief Start DAP server; returns bound port (0 on failure). */
    int  startDap(uint16_t port);
    void stopDap();
    /** @brief Start MCP server for AI tooling; returns bound port (0 on failure). */
    int  startMcp(uint16_t port);
    void stopMcp();
    void poll();

    /** @brief Draw DevTools AI ImGui panel when visible (call from UI/frame loop). */
    void drawAiPanel();
    /** @brief Draw DevTools console ImGui panel when visible (call from UI/frame loop). */
    void drawConsolePanel();

    bool isAttached() const { return vm_ != nullptr; }
    bool renderTraceEnabled() const { return renderTraceEnabled_; }
    bool sampleLocals() const { return sampleLocals_; }
    void setSampleLocals(bool on) { sampleLocals_ = on; }

    CallGraph&        graph() { return graph_; }
    const CallGraph&  graph() const { return graph_; }
    RenderFlow&       renderFlow() { return renderFlow_; }
    const RenderFlow& renderFlow() const { return renderFlow_; }
    Debugger&         debugger() { return Debugger::instance(); }
    Snapshot&         snapshot() { return Snapshot::instance(); }
    ScenarioRecorder& scenario() { return ScenarioRecorder::instance(); }
    DebugAdapter&     dap() { return DebugAdapter::instance(); }
    McpServer&        mcp();
    AiPanel&          ai();
    ConsolePanel&     console();

    SliceResult analyzeError(const std::string& errorMessage,
                             const std::vector<std::string>& hintVars = {}) const;

    std::string formatError(const std::string& errorMessage,
                            const std::vector<std::string>& hintVars = {}) const;

    const std::string& lastReport() const { return lastReport_; }
    const std::string& lastError() const { return lastError_; }
    /** @brief Script-side slice from the last `notifyError` (empty if none). */
    const SliceResult& lastSlice() const { return lastSlice_; }

    /** @brief Per-function script profile (line-hook timing). */
    struct ProfileEntry {
        int         calls = 0;
        int         lines = 0;
        long long   ns    = 0;
    };
    const std::unordered_map<std::string, ProfileEntry>& profile() const { return profile_; }
    void profileClear() {
        profile_.clear();
        profStack_.clear();
    }
    std::string formatProfileReport() const;

    /** @brief Record an error; includes script slice and render-pipeline slice when enabled. */
    std::string notifyError(const std::string& errorMessage,
                            const std::vector<std::string>& hintVars = {});

    void handleDebugEvent(HSQUIRRELVM vm, int type, const char* source, int line,
                          const char* funcname);

private:
    DevTool() = default;

    void sampleFrameLocals(HSQUIRRELVM vm, const SourceLoc& loc);
    void markErrorUses(const SourceLoc& loc, const std::vector<std::string>& hintVars);
    void installRenderTracer();
    void uninstallRenderTracer();
    /** @brief DAP poll + in-engine F5/F8/F10/F11 while blocked in a script pause. */
    void pumpWhilePaused();
    void handleDebugHotkey(const std::string& key);
    void profileLine(const std::string& func);
    void profileCall(const std::string& func);
    void profileReturn();

    HSQUIRRELVM vm_               = nullptr;
    bool        sampleLocals_     = true;
    bool        renderTraceEnabled_ = false;
    /** @brief Runtime whose boundary errors are routed into the slicer (if any). */
    eve::Runtime* runtime_        = nullptr;
    CallGraph   graph_;
    RenderFlow  renderFlow_;
    std::string lastReport_;
    std::string lastError_;
    SliceResult lastSlice_;
    std::unordered_map<std::string, ProfileEntry> profile_;
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> profStack_;

    std::unordered_map<int, std::unordered_map<std::string, std::string>> localSnap_;
};

}  // namespace eve::dev
