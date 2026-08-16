#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/AiPanel.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

using namespace eve::dev;

namespace {

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
        const int n = sock_.sendBytes(frame.data(), static_cast<int>(frame.size()));
        REQUIRE(n == static_cast<int>(frame.size()));
    }

    void sendRequest(int id, const std::string& method, const std::string& paramsJson = "{}") {
        std::ostringstream oss;
        oss << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"method\":\"" << method << "\"";
        if (!paramsJson.empty()) oss << ",\"params\":" << paramsJson;
        oss << "}";
        send(oss.str());
    }

    void sendNotification(const std::string& method, const std::string& paramsJson = "{}") {
        std::ostringstream oss;
        oss << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\"";
        if (!paramsJson.empty()) oss << ",\"params\":" << paramsJson;
        oss << "}";
        send(oss.str());
    }

    Poco::JSON::Object::Ptr recv(int timeoutMs = 2000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
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
            auto var = parser.parse(line);
            return var.extract<Poco::JSON::Object::Ptr>();
        }
        return nullptr;
    }

    Poco::JSON::Object::Ptr expectResult(int id, int timeoutMs = 2000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
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
                // Fail with the server error message visible in the assertion.
                auto err = msg->getObject("error");
                const std::string em =
                    err ? err->optValue<std::string>("message", "error") : "error";
                REQUIRE(em.empty());  // expected no error; message shown if present
                return nullptr;
            }
        }
        return nullptr;
    }

private:
    Poco::Net::StreamSocket sock_;
    std::string             recvBuf_;
};

}  // namespace

TEST_CASE("devtools.mcp.initializeToolsStatus") {
    auto& mcp = McpServer::instance();
    auto& dt  = DevTool::instance();
    auto& ai  = AiPanel::instance();

    mcp.stop();
    ai.clearLog();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    CHECK(ai.mcpPort() == port);

    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\","
                       "\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"eve-test\",\"version\":\"0\"}}");
    auto init = client.expectResult(1);
    REQUIRE(init);
    auto result = init->getObject("result");
    REQUIRE(result);
    CHECK(result->getValue<std::string>("protocolVersion") == "2025-06-18");
    CHECK(result->getObject("serverInfo")->getValue<std::string>("name") == "evengine");
    CHECK(result->has("capabilities"));

    client.sendNotification("notifications/initialized");
    McpServer::instance().poll();

    client.sendRequest(2, "tools/list");
    auto toolsMsg = client.expectResult(2);
    REQUIRE(toolsMsg);
    auto tools = toolsMsg->getObject("result")->getArray("tools");
    REQUIRE(tools);
    bool foundStatus = false;
    bool foundEval   = false;
    bool foundScene  = false;
    bool foundProc   = false;
    bool foundPhys   = false;
    bool foundShot   = false;
    bool foundParticles = false;
    bool foundAudio  = false;
    for (size_t i = 0; i < tools->size(); ++i) {
        auto t = tools->getObject(static_cast<unsigned>(i));
        const std::string name = t->getValue<std::string>("name");
        if (name == "eve_status") foundStatus = true;
        if (name == "eve_eval") foundEval = true;
        if (name == "eve_scene_status") foundScene = true;
        if (name == "eve_procgen_recipes") foundProc = true;
        if (name == "eve_physics_raycast") foundPhys = true;
        if (name == "eve_screenshot") foundShot = true;
        if (name == "eve_particles_emit") foundParticles = true;
        if (name == "eve_audio_set_volume") foundAudio = true;
    }
    CHECK(foundStatus);
    CHECK(foundEval);
    CHECK(foundScene);
    CHECK(foundProc);
    CHECK(foundPhys);
    CHECK(foundShot);
    CHECK(foundParticles);
    CHECK(foundAudio);

    client.sendRequest(3, "tools/call",
                       "{\"name\":\"eve_status\",\"arguments\":{}}");
    auto statusMsg = client.expectResult(3);
    REQUIRE(statusMsg);
    auto content = statusMsg->getObject("result")->getArray("content");
    REQUIRE(content);
    REQUIRE(content->size() >= 1);
    const std::string text = content->getObject(0)->getValue<std::string>("text");
    CHECK(text.find("\"attached\":true") != std::string::npos);
    CHECK(text.find("\"mcpPort\":") != std::string::npos);

    client.sendRequest(4, "tools/call",
                       "{\"name\":\"eve_ai_note\",\"arguments\":{\"text\":\"hello agent\"}}");
    REQUIRE(client.expectResult(4));
    const std::string log = ai.formatLog(16);
    CHECK(log.find("hello agent") != std::string::npos);
    CHECK(log.find("eve_status") != std::string::npos);

    client.sendRequest(5, "resources/read", "{\"uri\":\"eve://ai-session\"}");
    auto resMsg = client.expectResult(5);
    REQUIRE(resMsg);
    auto contents = resMsg->getObject("result")->getArray("contents");
    REQUIRE(contents);
    CHECK(contents->getObject(0)->getValue<std::string>("text").find("hello agent") !=
          std::string::npos);

    client.sendRequest(6, "prompts/list");
    auto promptsMsg = client.expectResult(6);
    REQUIRE(promptsMsg);
    auto prompts = promptsMsg->getObject("result")->getArray("prompts");
    REQUIRE(prompts);
    CHECK(prompts->size() >= 1);

    // New game-feature tools must return JSON text (not a hard JSON-RPC error),
    // even when the backing module is not mounted in this test process.
    auto textOf = [&](int id, const std::string& call) -> std::string {
        client.sendRequest(id, "tools/call", call);
        auto msg = client.expectResult(id);
        REQUIRE(msg);
        auto c = msg->getObject("result")->getArray("content");
        REQUIRE(c);
        REQUIRE(c->size() >= 1);
        return c->getObject(0)->getValue<std::string>("text");
    };
    const std::string sceneText = textOf(7, "{\"name\":\"eve_scene_status\",\"arguments\":{}}");
    CHECK(sceneText.find("eve_scene_status") == std::string::npos);  // tool names never echoed back
    CHECK(sceneText.find("{") != std::string::npos);
    const std::string procText = textOf(8, "{\"name\":\"eve_procgen_recipes\",\"arguments\":{}}");
    CHECK(procText.find("{") != std::string::npos);
    const std::string physText =
        textOf(9, "{\"name\":\"eve_physics_list_worlds\",\"arguments\":{}}");
    CHECK(physText.find("[") != std::string::npos);

    mcp.stop();
    dt.detach();
}

TEST_CASE("devtools.mcp.evalAndPause") {
    auto& mcp = McpServer::instance();
    auto& dt  = DevTool::instance();
    auto& dbg = Debugger::instance();

    mcp.stop();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    {
        auto script = vm.compileSource("score <- 42;\n");
        vm.run(script);
    }
    dt.attach(vm, false);

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"t\"}}");
    REQUIRE(client.expectResult(1));
    client.sendNotification("notifications/initialized");

    client.sendRequest(2, "tools/call",
                       "{\"name\":\"eve_eval\",\"arguments\":{\"expression\":\"score\"}}");
    auto evalMsg = client.expectResult(2);
    REQUIRE(evalMsg);
    const std::string evalText =
        evalMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>(
            "text");
    CHECK(evalText.find("42") != std::string::npos);

    client.sendRequest(3, "tools/call", "{\"name\":\"eve_pause\",\"arguments\":{}}");
    REQUIRE(client.expectResult(3));
    CHECK(dbg.isPaused());

    client.sendRequest(4, "tools/call", "{\"name\":\"eve_continue\",\"arguments\":{}}");
    REQUIRE(client.expectResult(4));
    CHECK(!dbg.isPaused());

    mcp.stop();
    dt.detach();
}

TEST_CASE("devtools.ai.panelLog") {
    auto& ai = AiPanel::instance();
    ai.clearLog();
    ai.setMcpPort(9999);
    ai.setMcpConnected(false);
    ai.addNote("panel-test");
    CHECK(ai.formatLog(8).find("panel-test") != std::string::npos);
    CHECK(ai.statusLine().find("9999") != std::string::npos);
    ai.setVisible(false);
    CHECK(!ai.isVisible());
    ai.toggleVisible();
    CHECK(ai.isVisible());
    ai.clearLog();
    ai.setMcpPort(0);
}
