// Soft restart over MCP: reset game state without dropping the resource cache.
//
// Agents debugging a live game must not kill the process between iterations —
// re-decoding assets is the expensive part. eve_restart keeps ResourceManager
// entries and re-enters soft_restart_game / eve_init with optional args.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/AiPanel.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/SoftRestart.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Exception.h>
#include <Poco/Timespan.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

namespace {

using eve::dev::AiPanel;
using eve::dev::DevTool;
using eve::dev::McpServer;
using eve::dev::SoftRestartRequest;
using eve::dev::executeSoftRestart;
using eve::dev::resourceCacheCount;

class McpClient {
public:
    explicit McpClient(int port) {
        Poco::Net::SocketAddress addr("127.0.0.1", static_cast<uint16_t>(port));
        sock_.connect(addr, Poco::Timespan(2, 0));
        sock_.setBlocking(true);
        sock_.setReceiveTimeout(Poco::Timespan(0, 50000));
        sock_.setSendTimeout(Poco::Timespan(2, 0));
        for (int i = 0; i < 100 && !McpServer::instance().hasClient(); ++i) {
            McpServer::instance().poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(McpServer::instance().hasClient());
    }

    void send(const std::string& json) {
        const std::string frame = json + "\n";
        const int         n     = sock_.sendBytes(frame.data(), static_cast<int>(frame.size()));
        REQUIRE(n == static_cast<int>(frame.size()));
    }

    void sendRequest(int id, const std::string& method, const std::string& paramsJson = "{}") {
        std::ostringstream oss;
        oss << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"method\":\"" << method << "\"";
        if (!paramsJson.empty()) oss << ",\"params\":" << paramsJson;
        oss << "}";
        send(oss.str());
    }

    Poco::JSON::Object::Ptr recv(int timeoutMs = 2000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            McpServer::instance().poll();
            char buf[8192];
            try {
                const int n = sock_.receiveBytes(buf, sizeof(buf));
                if (n > 0) recvBuf_.append(buf, static_cast<size_t>(n));
            } catch (const Poco::TimeoutException&) {
            } catch (const Poco::Net::NetException&) {
            }

            const auto nl = recvBuf_.find('\n');
            if (nl == std::string::npos) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            std::string line = recvBuf_.substr(0, nl);
            recvBuf_.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            Poco::JSON::Parser parser;
            return parser.parse(line).extract<Poco::JSON::Object::Ptr>();
        }
        return nullptr;
    }

    Poco::JSON::Object::Ptr expectResult(int id, int timeoutMs = 2000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = recv(50);
            if (!msg) continue;
            int msgId = -999;
            if (msg->has("id") && !msg->isNull("id")) {
                try {
                    msgId = msg->get("id").convert<int>();
                } catch (...) {
                    continue;
                }
            }
            if (msgId == id && msg->has("result")) return msg;
            if (msgId == id && msg->has("error")) {
                auto              err = msg->getObject("error");
                const std::string em  = err ? err->optValue<std::string>("message", "error") : "error";
                REQUIRE(em.empty());
                return nullptr;
            }
        }
        return nullptr;
    }

private:
    Poco::Net::StreamSocket sock_;
    std::string             recvBuf_;
};

std::string toolText(Poco::JSON::Object::Ptr response) {
    REQUIRE(response);
    auto result = response->getObject("result");
    REQUIRE(result);
    auto content = result->getArray("content");
    REQUIRE(content);
    REQUIRE(content->size() >= 1);
    return content->getObject(0)->getValue<std::string>("text");
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

void installSoftRestartStub(ssq::VM& vm) {
    vm.addTable("eve");
    DevTool::instance().exposeScriptApi(vm);
    vm.run(R"SQ(
restart_count <- 0
last_seed <- -1
function soft_restart_game(reload_scripts = true) {
    local args = ("restartArgs" in eve && eve.restartArgs != null) ? eve.restartArgs : {};
    restart_count += 1;
    if ("seed" in args) last_seed = args.seed.tointeger();
    if ("dev" in eve) eve.dev.resetNativeState();
    if ("eve_init" in getroottable()) eve_init();
    return true;
}
eve_init <- function() {}
)SQ");
}

}  // namespace

TEST_CASE("devtools.softRestart.keepsResourceCacheAndAppliesArgs") {
    auto& dt = DevTool::instance();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    dt.attach(vm, false);
    installSoftRestartStub(vm);

    const std::size_t  before = resourceCacheCount();
    SoftRestartRequest request;
    request.args = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    request.args->set("seed", 42);
    request.reloadScripts = true;

    auto result = executeSoftRestart(vm.getHandle(), request);
    REQUIRE(result);
    CHECK(result.value().scriptsReloaded);
    CHECK(result.value().initCalled);
    CHECK_EQ(result.value().resourceCountBefore, before);
    CHECK_EQ(result.value().resourceCountAfter, before);
    CHECK_EQ(vm.find("restart_count").toInt(), 1);
    CHECK_EQ(vm.find("last_seed").toInt(), 42);

    request.args->set("seed", 99);
    request.reloadScripts = false;
    auto again            = executeSoftRestart(vm.getHandle(), std::move(request));
    REQUIRE(again);
    CHECK(!again.value().scriptsReloaded);
    CHECK_EQ(again.value().resourceCountAfter, before);
    CHECK_EQ(vm.find("restart_count").toInt(), 2);
    CHECK_EQ(vm.find("last_seed").toInt(), 99);

    dt.detach();
}

TEST_CASE("devtools.mcp.eveRestartTool") {
    auto& mcp = McpServer::instance();
    auto& dt  = DevTool::instance();
    auto& ai  = AiPanel::instance();
    mcp.stop();
    ai.clearLog();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    dt.attach(vm, false);
    installSoftRestartStub(vm);

    const int port = mcp.listen(0);
    REQUIRE(port > 0);

    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\","
                       "\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"eve-test\",\"version\":\"0\"}}");
    REQUIRE(client.expectResult(1));

    client.sendRequest(2, "tools/list");
    auto list = client.expectResult(2);
    REQUIRE(list);
    bool foundRestart = false;
    {
        auto tools = list->getObject("result")->getArray("tools");
        REQUIRE(tools);
        for (unsigned i = 0; i < tools->size(); ++i) {
            auto tool = tools->getObject(i);
            if (tool && tool->getValue<std::string>("name") == "eve_restart") foundRestart = true;
        }
    }
    CHECK(foundRestart);

    const std::size_t before = resourceCacheCount();
    client.sendRequest(3, "tools/call",
                       "{\"name\":\"eve_restart\",\"arguments\":{\"args\":{\"seed\":7},\"reloadScripts\":true}}");
    auto call = client.expectResult(3);
    REQUIRE(call);
    auto body = parseObject(toolText(call));
    REQUIRE(body);
    CHECK(body->getValue<bool>("ok"));
    CHECK(body->getValue<bool>("keptResources"));
    CHECK_EQ(body->getValue<Poco::Int64>("resourceCountBefore"), static_cast<Poco::Int64>(before));
    CHECK_EQ(body->getValue<Poco::Int64>("resourceCountAfter"), static_cast<Poco::Int64>(before));
    CHECK(body->getValue<bool>("scriptsReloaded"));
    CHECK_EQ(vm.find("last_seed").toInt(), 7);
    CHECK_EQ(vm.find("restart_count").toInt(), 1);

    mcp.stop();
    dt.detach();
}
