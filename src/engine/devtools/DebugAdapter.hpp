#pragma once

#include "common/Export.h"
#include "devtools/Debugger.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Poco::Net {
class ServerSocket;
class StreamSocket;
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

    /** Accept clients + process one request batch (non-blocking). */
    void poll();

    /** Emit stopped event to the connected client (if any). */
    void notifyStopped(PauseReason reason, const SourceLoc& loc);
    void notifyContinued();
    void notifyTerminated();

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

    std::atomic<bool> listening_{false};
    std::atomic<bool> hasClient_{false};
    std::atomic<int>  port_{0};
    std::atomic<int>  seq_{1};
    std::mutex        ioMu_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::unique_ptr<Poco::Net::StreamSocket> client_;
    std::string       recvBuf_;
    bool              configured_ = false;
    int               varRefLocals_ = 1;
    int               varRefWatches_ = 2;
    std::vector<VariableInfo> varCache_;
};

}  // namespace eve::dev
