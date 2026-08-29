#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "devtools/AiPanel.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#include "editor/Editor.h"
#include "ui/EditorHost.h"

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
#include <filesystem>
#include <fstream>
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

    void sendNotification(const std::string& method, const std::string& paramsJson = "{}") {
        std::ostringstream oss;
        oss << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\"";
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
            auto               var = parser.parse(line);
            return var.extract<Poco::JSON::Object::Ptr>();
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
                // Fail with the server error message visible in the assertion.
                auto              err = msg->getObject("error");
                const std::string em  = err ? err->optValue<std::string>("message", "error") : "error";
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

    eve::editor::Editor            editor;
    eve::editor::CommandDescriptor editorCommand;
    editorCommand.id          = eve::editor::CommandId("mcp.test-command");
    editorCommand.ownerModule = "mcp.test";
    editorCommand.displayName = "MCP test command";
    REQUIRE(editor.commandService()
                .registerCommand(std::move(editorCommand),
                                 [](const eve::editor::CommandContext&, const eve::editor::EditorValue&) {
                                     return eve::editor::EditorResult<eve::editor::EditorValue>::applied(
                                         eve::editor::EditorValue("executed"));
                                 })
                .accepted());

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
    bool foundStatus          = false;
    bool foundEval            = false;
    bool foundScene           = false;
    bool foundProc            = false;
    bool foundPhys            = false;
    bool foundShot            = false;
    bool foundParticles       = false;
    bool foundAudio           = false;
    bool foundVision          = false;
    bool foundVcfg            = false;
    bool foundSdMod           = false;
    bool foundSdInfo          = false;
    bool foundCamGen          = false;
    bool foundReset           = false;
    bool foundSdStat          = false;
    bool foundSdInst          = false;
    bool foundUiTree          = false;
    bool foundUiGet           = false;
    bool foundUiClick         = false;
    bool foundHostReload      = false;
    bool foundHotReloadStatus = false;
    bool foundEditorCommands  = false;
    bool foundEditorPlan      = false;
    bool foundSkeletonInspect = false;
    for (size_t i = 0; i < tools->size(); ++i) {
        auto              t    = tools->getObject(static_cast<unsigned>(i));
        const std::string name = t->getValue<std::string>("name");
        if (name == "eve_status") foundStatus = true;
        if (name == "eve_eval") foundEval = true;
        if (name == "eve_scene_status") foundScene = true;
        if (name == "eve_procgen_recipes") foundProc = true;
        if (name == "eve_physics_raycast") foundPhys = true;
        if (name == "eve_screenshot") foundShot = true;
        if (name == "eve_particles_emit") foundParticles = true;
        if (name == "eve_audio_set_volume") foundAudio = true;
        if (name == "eve_render_describe") foundVision = true;
        if (name == "eve_render_vision_config") foundVcfg = true;
        if (name == "eve_scene_modify") foundSdMod = true;
        if (name == "eve_scene_info") foundSdInfo = true;
        if (name == "eve_camera_generate") foundCamGen = true;
        if (name == "eve_scene_reset") foundReset = true;
        if (name == "eve_scene_director_status") foundSdStat = true;
        if (name == "eve_scene_director_install") foundSdInst = true;
        if (name == "eve_ui_tree") foundUiTree = true;
        if (name == "eve_ui_get") foundUiGet = true;
        if (name == "eve_ui_click") foundUiClick = true;
        if (name == "eve_host_resource_reload") foundHostReload = true;
        if (name == "eve_host_hot_reload_status") foundHotReloadStatus = true;
        if (name == "eve_editor_commands") foundEditorCommands = true;
        if (name == "eve_editor_plan") foundEditorPlan = true;
        if (name == "eve_skeleton_inspect") foundSkeletonInspect = true;
    }
    CHECK(foundStatus);
    CHECK(foundEval);
    CHECK(foundScene);
    CHECK(foundProc);
    CHECK(foundPhys);
    CHECK(foundShot);
    CHECK(foundParticles);
    CHECK(foundAudio);
    CHECK(foundVision);
    CHECK(foundVcfg);
    CHECK(foundSdMod);
    CHECK(foundSdInfo);
    CHECK(foundCamGen);
    CHECK(foundReset);
    CHECK(foundSdStat);
    CHECK(foundSdInst);
    CHECK(foundUiTree);
    CHECK(foundUiGet);
    CHECK(foundUiClick);
    CHECK(foundHostReload);
    CHECK(foundHotReloadStatus);
    CHECK(foundEditorCommands);
    CHECK(foundEditorPlan);
    CHECK(foundSkeletonInspect);

    client.sendRequest(3, "tools/call", "{\"name\":\"eve_status\",\"arguments\":{}}");
    auto statusMsg = client.expectResult(3);
    REQUIRE(statusMsg);
    auto content = statusMsg->getObject("result")->getArray("content");
    REQUIRE(content);
    REQUIRE(content->size() >= 1);
    const std::string text = content->getObject(0)->getValue<std::string>("text");
    CHECK(text.find("\"attached\":true") != std::string::npos);
    CHECK(text.find("\"mcpPort\":") != std::string::npos);

    client.sendRequest(4, "tools/call", "{\"name\":\"eve_ai_note\",\"arguments\":{\"text\":\"hello agent\"}}");
    REQUIRE(client.expectResult(4));
    const std::string log = ai.formatLog(16);
    CHECK(log.find("hello agent") != std::string::npos);
    CHECK(log.find("eve_status") != std::string::npos);

    client.sendRequest(5, "resources/read", "{\"uri\":\"eve://ai-session\"}");
    auto resMsg = client.expectResult(5);
    REQUIRE(resMsg);
    auto contents = resMsg->getObject("result")->getArray("contents");
    REQUIRE(contents);
    CHECK(contents->getObject(0)->getValue<std::string>("text").find("hello agent") != std::string::npos);

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
    const std::string physText = textOf(9, "{\"name\":\"eve_physics_list_worlds\",\"arguments\":{}}");
    CHECK(physText.find("[") != std::string::npos);
    // Vision config tool returns JSON config (no network call, no crash).
    const std::string vcfgText = textOf(10, "{\"name\":\"eve_render_vision_config\",\"arguments\":{}}");
    CHECK(vcfgText.find("{") != std::string::npos);
    CHECK(vcfgText.find("apiKeySet") != std::string::npos);

    // Scene-director tools: auto-install the Squirrel kit into the plain test VM
    // and drive it over MCP (no Graphics / modules mounted -> geometry-only ops).
    const std::string sdStat = textOf(11, "{\"name\":\"eve_scene_director_status\",\"arguments\":{}}");
    CHECK(sdStat.find("\"installed\":true") != std::string::npos);

    const std::string resetText = textOf(12, "{\"name\":\"eve_scene_reset\",\"arguments\":{}}");
    CHECK(resetText.find("ok") != std::string::npos);

    const std::string infoText = textOf(13, "{\"name\":\"eve_scene_info\",\"arguments\":{}}");
    CHECK(infoText.find("\"count\":0") != std::string::npos);

    const std::string camText = textOf(14, "{\"name\":\"eve_camera_generate\",\"arguments\":{\"count\":2}}");
    CHECK(camText.find("\"cam_0\"") != std::string::npos);

    const std::string modInfo = textOf(15, "{\"name\":\"eve_scene_modify\",\"arguments\":{\"action\":\"info\"}}");
    CHECK(modInfo.find("\"count\":0") != std::string::npos);
    // Unknown action must surface as a controlled error, not crash the server.
    const std::string modBad = textOf(16, "{\"name\":\"eve_scene_modify\",\"arguments\":{\"action\":\"no_such\"}}");
    const bool        modBadHasError =
        modBad.find("error") != std::string::npos || modBad.find("unknown action") != std::string::npos;
    CHECK(modBadHasError);
    const std::string editorCommands = textOf(17, "{\"name\":\"eve_editor_commands\",\"arguments\":{}}");
    CHECK(editorCommands.find("mcp.test-command") != std::string::npos);
    const std::string editorExecuted =
        textOf(18, "{\"name\":\"eve_editor_execute\",\"arguments\":{\"command\":\"mcp.test-command\",\"payload\":{}}}");
    CHECK(editorExecuted.find("\"status\":\"applied\"") != std::string::npos);

    mcp.stop();
    dt.detach();
}

TEST_CASE("devtools.mcp.evalAndPause") {
    auto& mcp = McpServer::instance();
    auto& dt  = DevTool::instance();
    auto& dbg = Debugger::instance();

    mcp.stop();
    dt.detach();

    const auto projectRoot =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_eval_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(projectRoot);

    eve::Runtime runtime(1024, ssq::Libs::ALL);
    runtime.runSource(
        "score <- 42;\n"
        "eve_mcp_skeleton_inspect <- function(actor,bone) { "
        "return \"{\\\"actor\\\":\\\"\" + actor + \"\\\",\\\"bone\\\":\\\"\" + bone + \"\\\"}\"; };\n",
        "game:/main.nut");
    dt.attach(runtime, false);
    mcp.setGameRoot(projectRoot.string());

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"t\"}}");
    REQUIRE(client.expectResult(1));
    client.sendNotification("notifications/initialized");

    client.sendRequest(2, "tools/call", "{\"name\":\"eve_eval\",\"arguments\":{\"expression\":\"score\"}}");
    auto evalMsg = client.expectResult(2);
    REQUIRE(evalMsg);
    const std::string evalText =
        evalMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>("text");
    CHECK(evalText.find("42") != std::string::npos);

    client.sendRequest(3, "tools/call", "{\"name\":\"eve_run_script\",\"arguments\":{\"source\":\"score = 43;\"}}");
    auto runMsg = client.expectResult(3);
    REQUIRE(runMsg);
    const std::string runText =
        runMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>("text");
    CHECK(runText == "ok");

    // One-shot MCP source must remain in memory: no user-space script and no
    // persistent compiler identity that can collide with project hot reload.
    CHECK(runtime.scriptCompiler().metadata("mcp_snippet.nut") == nullptr);
    CHECK(runtime.scriptCompiler().metadata("eval") == nullptr);
    bool foundScript = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".nut") foundScript = true;
    }
    CHECK(!foundScript);

    client.sendRequest(4, "tools/call",
                       "{\"name\":\"eve_skeleton_inspect\",\"arguments\":{\"actor\":\"Lyra\",\"bone\":\"handslot.r\"}}");
    auto skeletonMsg = client.expectResult(4);
    REQUIRE(skeletonMsg);
    const std::string skeletonText = skeletonMsg->getObject("result")
                                         ->getArray("content")
                                         ->getObject(0)
                                         ->getValue<std::string>("text");
    CHECK(skeletonText.find("\"actor\":\"Lyra\"") != std::string::npos);
    CHECK(skeletonText.find("\"bone\":\"handslot.r\"") != std::string::npos);

    client.sendRequest(5, "tools/call", "{\"name\":\"eve_pause\",\"arguments\":{}}");
    REQUIRE(client.expectResult(5));
    CHECK(dbg.isPaused());

    client.sendRequest(6, "tools/call", "{\"name\":\"eve_continue\",\"arguments\":{}}");
    REQUIRE(client.expectResult(6));
    CHECK(!dbg.isPaused());

    mcp.stop();
    dt.detach();
    mcp.setGameRoot({});
    std::filesystem::remove_all(projectRoot);
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

TEST_CASE("devtools.mcp.stdioTransport") {
    auto& mcp = McpServer::instance();
    mcp.stop();

    std::stringstream in, out;
    REQUIRE(mcp.listenStdio(in, out));
    in << "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
          "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
          "\"clientInfo\":{\"name\":\"stdio-test\",\"version\":\"0\"}}}\n";
    in << "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n";
    in << "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
          "\"name\":\"eve_host_status\",\"arguments\":{}}}\n";

    for (int i = 0; i < 100; ++i) {
        mcp.poll();
        if (out.str().find("\"id\":3") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const std::string resp = out.str();
    CHECK(resp.find("\"id\":1") != std::string::npos);
    CHECK(resp.find("evengine") != std::string::npos);
    CHECK(resp.find("\"id\":2") != std::string::npos);
    CHECK(resp.find("eve_host_editor_apply") != std::string::npos);
    CHECK(resp.find("eve_host_shutdown") != std::string::npos);
    CHECK(resp.find("\"id\":3") != std::string::npos);
    CHECK(resp.find("eve_host_status") != std::string::npos);

    in.setstate(std::ios::eofbit);
    mcp.stop();
}

TEST_CASE("devtools.mcp.hostEditorBinding") {
    auto& mcp  = McpServer::instance();
    auto& dt   = DevTool::instance();
    auto& host = eve::ui::EditorHost::instance();
    mcp.stop();
    dt.detach();
    host.stop();

    const auto tmp = std::filesystem::temp_directory_path() /
                     ("eve_host_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp);

    ssq::VM vm(1024, ssq::Libs::ALL);
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);
    host.start(vm, tmp.string(), /*allowWindow=*/false);
    host.exposeScriptApi(vm);

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"host-test\"}}");
    REQUIRE(client.expectResult(1));
    client.sendNotification("notifications/initialized");

    auto textOf = [&](int id, const std::string& call) -> std::string {
        client.sendRequest(id, "tools/call", call);
        auto msg = client.expectResult(id);
        REQUIRE(msg);
        auto c = msg->getObject("result")->getArray("content");
        REQUIRE(c);
        REQUIRE(c->size() >= 1);
        return c->getObject(0)->getValue<std::string>("text");
    };

    // Register a Squirrel ViewModel with a bound property + onChange callback.
    const std::string vmSource =
        "::TerrainVM <- {"
        "  brushSize = 5,"
        "  changedCount = 0,"
        "  onChange = function(widget, value) {"
        "    if (widget == \"brushSize\") this.changedCount++;"
        "  }"
        "};";
    std::string vmSourceEscaped;
    for (char c : vmSource) {
        if (c == '\\')
            vmSourceEscaped += "\\\\";
        else if (c == '"')
            vmSourceEscaped += "\\\"";
        else
            vmSourceEscaped += c;
    }
    const std::string vmText = textOf(2,
                                      "{\"name\":\"eve_host_vm_register\",\"arguments\":{\"name\":\"TerrainVM\","
                                      "\"source\":\"" +
                                          vmSourceEscaped + "\"}}");
    CHECK(vmText == "ok");

    const std::string editorJson =
        "{\"id\":\"terrain\",\"title\":\"Terrain Editor\",\"vm\":\"TerrainVM\","
        "\"children\":["
        "{\"type\":\"slider\",\"id\":\"brushSize\",\"label\":\"Brush\",\"min\":0,\"max\":64,"
        "\"value\":5,\"bind\":\"vm.brushSize\",\"onChange\":\"vm.onChange\"},"
        "{\"type\":\"button\",\"id\":\"apply\",\"label\":\"Apply\",\"command\":\"vm.apply\"}"
        "]}";
    const std::string applyText =
        textOf(3, "{\"name\":\"eve_host_editor_apply\",\"arguments\":{\"editor\":" + editorJson + "}}");
    CHECK(applyText.find("\"id\":\"terrain\"") != std::string::npos);
    CHECK(applyText.find("\"brushSize\"") != std::string::npos);

    // set_value must write the VM, fire onChange, and emit a change event.
    const std::string setText = textOf(4,
                                       "{\"name\":\"eve_host_editor_set_value\",\"arguments\":{"
                                       "\"editor\":\"terrain\",\"widget\":\"brushSize\",\"value\":12}}");
    CHECK(setText == "ok");

    const std::string stateText = textOf(5, "{\"name\":\"eve_host_editor_state\",\"arguments\":{\"id\":\"terrain\"}}");
    CHECK(stateText.find("\"brushSize\":12") != std::string::npos);

    const std::string evalText =
        textOf(6, "{\"name\":\"eve_eval\",\"arguments\":{\"expression\":\"TerrainVM.brushSize\"}}");
    CHECK(evalText.find("12") != std::string::npos);

    const std::string countText =
        textOf(7, "{\"name\":\"eve_eval\",\"arguments\":{\"expression\":\"TerrainVM.changedCount\"}}");
    CHECK(countText.find("1") != std::string::npos);

    const std::string eventsText = textOf(8, "{\"name\":\"eve_host_events\",\"arguments\":{\"editor\":\"terrain\"}}");
    CHECK(eventsText.find("\"type\":\"change\"") != std::string::npos);
    CHECK(eventsText.find("\"brushSize\"") != std::string::npos);

    // Persistence: save -> unload -> reload from disk.
    CHECK(textOf(9, "{\"name\":\"eve_host_editor_save\",\"arguments\":{\"id\":\"terrain\"}}") == "ok");
    CHECK(std::filesystem::is_regular_file(tmp / "editors" / "terrain.editor.json"));
    CHECK(std::filesystem::is_regular_file(tmp / "editors" / "terrain.vm.nut"));

    CHECK(textOf(10, "{\"name\":\"eve_host_editor_unload\",\"arguments\":{\"id\":\"terrain\"}}") == "ok");
    const std::string listText = textOf(11, "{\"name\":\"eve_host_editor_list\",\"arguments\":{}}");
    CHECK(listText.find("terrain") == std::string::npos);

    host.loadEditorsFromDisk();
    const std::string reloadList = textOf(12, "{\"name\":\"eve_host_editor_list\",\"arguments\":{}}");
    CHECK(reloadList.find("terrain") != std::string::npos);
    const std::string reloadState =
        textOf(13, "{\"name\":\"eve_host_editor_state\",\"arguments\":{\"id\":\"terrain\"}}");
    // Persistence stores the original View + VM source; runtime values reset.
    CHECK(reloadState.find("\"brushSize\":5") != std::string::npos);

    host.stop();
    mcp.stop();
    dt.detach();
    std::filesystem::remove_all(tmp);
}
