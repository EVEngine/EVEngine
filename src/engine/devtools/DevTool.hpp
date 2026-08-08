#pragma once

#include "common/Export.h"
#include "devtools/CallGraph.hpp"

#include <memory>
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
 * Platform-level script debugger / dynamic slicer front-end.
 *
 * When attached to a Squirrel VM (typically via `eve run --debug`):
 *  - enables line debug info
 *  - installs a native debug hook that feeds CallGraph (call/return/line)
 *  - optionally samples locals each line to infer Def events (data flow)
 *  - on errors, builds a Weiser-style backward slice + call stack report
 *
 * Not shipped on Android/iOS trimmed runtimes (EVDevTools is desktop-only).
 */
class EVENGINE_API DevTool {
public:
    static DevTool& instance();

    DevTool(const DevTool&)            = delete;
    DevTool& operator=(const DevTool&) = delete;

    /** Attach tracer to VM. Safe to call once per VM; replaces prior hook. */
    void attach(ssq::VM& vm, bool sampleLocals = true);
    void attach(HSQUIRRELVM vm, bool sampleLocals = true);
    void detach();

    bool isAttached() const { return vm_ != nullptr; }
    bool sampleLocals() const { return sampleLocals_; }
    void setSampleLocals(bool on) { sampleLocals_ = on; }

    CallGraph&       graph() { return graph_; }
    const CallGraph& graph() const { return graph_; }

    /**
     * Capture stack from the live VM (sq_stackinfos) and run a backward slice.
     * `hintVars` names variables implicated in the failure (may be empty).
     */
    SliceResult analyzeError(const std::string& errorMessage,
                             const std::vector<std::string>& hintVars = {}) const;

    /** Same as analyzeError, formatted for stderr / log. */
    std::string formatError(const std::string& errorMessage,
                            const std::vector<std::string>& hintVars = {}) const;

    /** Last report produced by formatError / notifyError. */
    const std::string& lastReport() const { return lastReport_; }

    /** Record an error and remember the report (also returns it). */
    std::string notifyError(const std::string& errorMessage,
                            const std::vector<std::string>& hintVars = {});

    // --- called from the native debug hook (public for the C callback) -----
    void handleDebugEvent(HSQUIRRELVM vm, int type, const char* source, int line,
                          const char* funcname);

private:
    DevTool() = default;

    void sampleFrameLocals(HSQUIRRELVM vm, const SourceLoc& loc);
    /** Emit Use events for current locals at the error site before slicing. */
    void markErrorUses(const SourceLoc& loc, const std::vector<std::string>& hintVars);

    HSQUIRRELVM vm_          = nullptr;
    bool        sampleLocals_ = true;
    CallGraph   graph_;
    std::string lastReport_;

    // Previous local snapshot per stack depth: name → type+repr
    std::unordered_map<int, std::unordered_map<std::string, std::string>> localSnap_;
};

}  // namespace eve::dev
