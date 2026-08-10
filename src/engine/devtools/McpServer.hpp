#pragma once

#include "common/Export.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace Poco::Net {
class ServerSocket;
class StreamSocket;
}

namespace eve::dev {

/**
 * Embedded Model Context Protocol (MCP) server for AI-assisted game development.
 *
 * Speaks JSON-RPC 2.0 over TCP with newline-delimited messages (same wire shape
 * as MCP stdio; no embedded newlines in payloads). Start with listen(port)
 * after DevTool::attach; call poll() from the game loop / pause pump.
 *
 * Capabilities: tools, resources, prompts for AI test workflows.
 * Cursor / Claude Desktop can attach via tools/eve-mcp (stdio ↔ TCP bridge).
 *
 * Desktop-only (part of EVDevTools); not shipped on Android/iOS trimmed runtimes.
 */
class EVENGINE_API McpServer {
public:
    static McpServer& instance();

    McpServer(const McpServer&)            = delete;
    McpServer& operator=(const McpServer&) = delete;

    /** Bind TCP listen port (0 = ephemeral). Returns bound port or 0 on failure. */
    int  listen(uint16_t port);
    void stop();
    bool isListening() const { return listening_.load(); }
    int  port() const { return port_.load(); }
    bool hasClient() const { return hasClient_.load(); }

    /** Accept clients + process one request batch (non-blocking). */
    void poll();

    /** Game / project directory hint for agents. */
    void setGameRoot(std::string root);
    const std::string& gameRoot() const { return gameRoot_; }

private:
    McpServer();
    ~McpServer();

    void acceptNonBlocking();
    void readAndDispatch();
    bool sendLine(const std::string& json);
    void handleMessage(const std::string& json);

    std::atomic<bool> listening_{false};
    std::atomic<bool> hasClient_{false};
    std::atomic<int>  port_{0};
    std::mutex        ioMu_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::unique_ptr<Poco::Net::StreamSocket> client_;
    std::string       recvBuf_;
    std::string       gameRoot_;
    bool              initialized_ = false;
    std::string       protocolVersion_ = "2025-06-18";
};

}  // namespace eve::dev
