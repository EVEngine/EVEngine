#pragma once

#include "common/Export.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

namespace Poco::Net {
class ServerSocket;
class StreamSocket;
}

namespace eve::dev {

/**
 * @brief Embedded Model Context Protocol (MCP) server for AI-assisted game development.
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

    enum class Transport { None, Tcp, Stdio };

    /** @brief Bind TCP listen port (0 = ephemeral). Returns bound port or 0 on failure. */
    int  listen(uint16_t port);
    /**
     * @brief Switch to stdio transport (MCP stdio server). A reader thread pulls
     * newline-delimited JSON from `in` into a queue that poll() drains on the
     * caller thread, so tool execution stays on the main / render thread.
     * Responses are written to `out` (stdout by default — keep it MCP-only).
     * Returns true when the transport starts.
     */
    bool listenStdio(std::istream& in = std::cin, std::ostream& out = std::cout);
    void stop();
    bool isListening() const { return listening_.load(); }
    int  port() const { return port_.load(); }
    bool hasClient() const { return hasClient_.load(); }
    Transport transport() const { return transport_.load(); }
    /** @brief True once the stdio input reached EOF (host should exit). */
    bool stdinClosed() const { return stdinClosed_.load(); }

    /** @brief Accept clients + process one request batch (non-blocking). */
    void poll();

    /** @brief Game / project directory hint for agents. */
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
    std::atomic<Transport> transport_{Transport::None};
    std::atomic<bool> stdinClosed_{false};
    std::mutex        ioMu_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::unique_ptr<Poco::Net::StreamSocket> client_;
    std::istream*     stdioIn_  = nullptr;
    std::ostream*     stdioOut_ = nullptr;
    std::vector<std::string> stdioQueue_;
    std::thread       stdioReader_;
    bool              joinReader_ = false;
    std::string       recvBuf_;
    std::string       gameRoot_;
    bool              initialized_ = false;
    std::string       protocolVersion_ = "2025-06-18";
};

}  // namespace eve::dev
