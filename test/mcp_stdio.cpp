#include "devtools/McpServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <sstream>
#include <thread>

TEST_CASE("devtools.mcp.stdioTransport") {
    auto &mcp = eve::dev::McpServer::instance();
    mcp.stop();

    // Finish the input before starting its reader. Concurrent stringstream
    // writes race with getline and can leave the reader permanently at EOF.
    std::istringstream in(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"stdio-test\",\"version\":\"0\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"eve_host_status\",\"arguments\":{}}}\n");
    std::ostringstream out;
    struct StopReader {
        eve::dev::McpServer &server;
        ~StopReader() { server.stop(); }
    } stopReader{mcp};
    REQUIRE(mcp.listenStdio(in, out));

    for (int i = 0; i < 100; ++i) {
        mcp.poll();
        if (out.str().find("\"id\":3") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    mcp.stop();
    const std::string resp = out.str();
    REQUIRE(resp.find("\"id\":1") != std::string::npos);
    REQUIRE(resp.find("evengine") != std::string::npos);
    REQUIRE(resp.find("\"id\":2") != std::string::npos);
    REQUIRE(resp.find("eve_host_editor_apply") != std::string::npos);
    REQUIRE(resp.find("eve_host_shutdown") != std::string::npos);
    REQUIRE(resp.find("\"id\":3") != std::string::npos);
    REQUIRE(resp.find("eve_host_status") != std::string::npos);
}
