#pragma once

#include "common/Export.h"
#include "devtools/Debugger.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Poco/SharedPtr.h>

namespace Poco::Net {
class ServerSocket;
class StreamSocket;
}

namespace Poco::JSON {
class Object;
}

namespace eve::dev {

/**
 * Minimal Debug Adapter Protocol (DAP) server for VS Code.
 *
 * Speaks Content-Length framed JSON over TCP. Start with listen(port) after
 * DevTool::attach; call poll() from the game loop / pause wait pump.
 *
 * Supported requests: initialize, launch/attach, setBreakpoints,
 * configurationDone, threads, stackTrace, scopes, variables, continue,
 * next/stepIn/stepOut, pause, evaluate, disconnect.
 */
class EVENGINE_API DebugAdapter {
public:
    static DebugAdapter& instance();

    DebugAdapter(const DebugAdapter&)            = delete;
    DebugAdapter& operator=(const DebugAdapter&) = delete;

    /** Bind TCP listen port (0 = ephemeral). Returns bound port or 0 on failure. */
    int  listen(uint16_t port);
    void stop();
    bool isListening() const { return listening_.load(); }
    int  port() const { return port_.load(); }
    bool hasClient() const { return hasClient_.load(); }

    /** Game directory used to resolve relative script paths in stack frames. */
    void setSourceRoot(std::string root);
    const std::string& sourceRoot() const { return sourceRoot_; }
    /** Register virtual source content for compiled buffers (compilestring). */
    void registerSource(std::string name, std::string content);

    /** Accept clients + process one request batch (non-blocking). */
    void poll();

    /** True after the IDE sends configurationDone (breakpoints are installed). */
    bool isConfigured() const { return configured_; }

    /**
     * Block briefly until a DAP client finishes initialize/launch/setBreakpoints/
     * configurationDone so scripts do not race past early breakpoints.
     * Returns true if configured, false on timeout (game still starts).
     */
    bool waitUntilConfigured(int timeoutMs = 15000);

    /** Emit stopped event to the connected client (if any). */
    void notifyStopped(PauseReason reason, const SourceLoc& loc,
                       const std::string& description = {});
    void notifyContinued();
    void notifyTerminated();
    /** Last exception message delivered via notifyStopped(Exception, ...). */
    const std::string& lastException() const { return lastException_; }

private:
    // Defined in .cpp so unique_ptr<incomplete Poco sockets> is legal on MSVC.
    DebugAdapter();
    ~DebugAdapter();

    void acceptNonBlocking();
    void readAndDispatch();
    bool sendMessage(const std::string& json);
    void handleRequest(const std::string& json);
    std::string makeResponse(int seq, int requestSeq, const std::string& command, bool ok,
                             const std::string& bodyJson, const std::string& message = {});
    std::string makeEvent(const std::string& event, const std::string& bodyJson);
    static std::string reasonString(PauseReason r);
    /** Make a path VS Code can open (absolute when possible). */
    std::string resolveSourcePath(std::string source) const;
    /** Remember IDE absolute paths so Squirrel relative names can be remapped. */
    void rememberSourcePath(const std::string& path);
    /** Best-effort: map embedded `load.nut` to repo `src/scripts/load.nut`. */
    void discoverEngineScriptAliases();
    /** DAP `breakpoint` event: verification state changed for a breakpoint. */
    void handleBreakpointEvent(int id, const std::string& source, int line, bool verified);
    /** DAP variablesReference for a call-stack frame's locals scope. */
    int varRefForFrame(int frameId);
    /** Register an expandable child scope and return its variablesReference. */
    int allocVarRef(int kind, int frame, std::vector<std::string> path);
    /** Source object for a stack frame / stopped event (path or virtual ref). */
    Poco::SharedPtr<Poco::JSON::Object> makeSourceObject(const std::string& source) const;

    struct VarScope {
        int kind = 0;  // 0 = locals, 1 = globals
        int frame = 0;
        std::vector<std::string> path;
    };

    std::atomic<bool> listening_{false};
    std::atomic<bool> hasClient_{false};
    std::atomic<int>  port_{0};
    std::atomic<int>  seq_{1};
    std::recursive_mutex ioMu_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::unique_ptr<Poco::Net::StreamSocket> client_;
    std::string       recvBuf_;
    std::string       sourceRoot_;
    /** basename / relative → absolute path from setBreakpoints / launch. */
    std::unordered_map<std::string, std::string> sourceAliases_;
    bool              configured_ = false;
    bool              stopOnEntry_ = false;
    int               varRefLocals_ = 1;
    int               varRefWatches_ = 2;
    int               varRefGlobals_ = 5000;
    int               varRefFrameBase_ = 1000;
    int               nextVarRef_ = 10000;
    std::vector<VariableInfo> varCache_;
    std::unordered_map<int, VarScope> varScopes_;
    std::string       lastException_;
    std::unordered_map<std::string, std::string> sourceContents_;
    std::unordered_map<int, std::string> sourceRefContents_;
};

}  // namespace eve::dev
