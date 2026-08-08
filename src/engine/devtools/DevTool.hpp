#pragma once

#include "common/Export.h"
#include "devtools/CallGraph.hpp"
#include "devtools/RenderFlow.hpp"

#include <string>
#include <unordered_map>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace ssq {
class VM;
}

namespace eve::dev {

/**
 * Platform-level script + render debugger / dynamic slicer front-end.
 *
 * When attached (typically via `eve run --debug`):
 *  - Squirrel: debug hook → CallGraph (call stack + Def/Use data-flow)
 *  - Render: installs RenderFlow as eve::debug::IRenderTracer
 *  - on errors: Weiser-style backward slice for script and/or render pipeline
 *
 * Not shipped on Android/iOS trimmed runtimes (EVDevTools is desktop-only).
 */
class EVENGINE_API DevTool {
public:
    static DevTool& instance();

    DevTool(const DevTool&)            = delete;
    DevTool& operator=(const DevTool&) = delete;

    /** Attach script tracer to VM and enable render tracing. */
    void attach(ssq::VM& vm, bool sampleLocals = true);
    void attach(HSQUIRRELVM vm, bool sampleLocals = true);
    /** Enable render-flow tracing without a Squirrel VM (C++ / unit tests). */
    void enableRenderTrace(bool on = true);
    void detach();

    bool isAttached() const { return vm_ != nullptr; }
    bool renderTraceEnabled() const { return renderTraceEnabled_; }
    bool sampleLocals() const { return sampleLocals_; }
    void setSampleLocals(bool on) { sampleLocals_ = on; }

    CallGraph&        graph() { return graph_; }
    const CallGraph&  graph() const { return graph_; }
    RenderFlow&       renderFlow() { return renderFlow_; }
    const RenderFlow& renderFlow() const { return renderFlow_; }

    SliceResult analyzeError(const std::string& errorMessage,
                             const std::vector<std::string>& hintVars = {}) const;

    std::string formatError(const std::string& errorMessage,
                            const std::vector<std::string>& hintVars = {}) const;

    const std::string& lastReport() const { return lastReport_; }

    /** Record an error; includes script slice and render-pipeline slice when enabled. */
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

    HSQUIRRELVM vm_               = nullptr;
    bool        sampleLocals_     = true;
    bool        renderTraceEnabled_ = false;
    CallGraph   graph_;
    RenderFlow  renderFlow_;
    std::string lastReport_;

    std::unordered_map<int, std::unordered_map<std::string, std::string>> localSnap_;
};

}  // namespace eve::dev
