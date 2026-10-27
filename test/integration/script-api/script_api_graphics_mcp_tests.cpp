// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "cases": {
    "runtime-lifecycle": [
      "devtools.procgenQuery.generateMapReportsStructuredFailure",
      "devtools.sceneQuery.getSetReportStructuredFailure",
      "devtools.renderCapture.savePngReportsStructuredFailure",
      "devtools.ai.panelLog",
      "devtools.mcp.engineLineDropsScriptEchoOnly",
      "ModuleManager.RegistryRollbackOwnsCreatedInstance",
      "ModuleManager.RegistryConflictPreservesCommittedInstance",
      "SimpleSquirrelTest.RemovingAClassFieldFallsThroughToTheMetamethods"
    ],
    "platform-network": [
      "devtools.mcp.executeObserveCorrelatesEditorTransactionWithLiveRuntime",
      "devtools.mcp.executeObserveCorrelatesSceneEditorWithNamedHost",
      "devtools.mcp.rxObservationSessionTracksChangesDeduplicatesAndCloses"
    ],
    "concurrency": [
      "devtools.mcp.crashReportExplainsPreviousRun",
      "procgen.worker.legacyIssueRejectsWrongThread",
      "procgen.runtimeGeneration.movesValueJobsAcrossWorkerBoundary",
      "procgen.runtimeGeneration.rejectsWorkerThreadSchedulerMutation"
    ],
    "render-offscreen": [
      "moduleExpose.allBindingsPreserveProcgenRockSurface"
    ],
    "render-window": [
      "procgen.animation.pcgSkinnedMesh.realGraphicsAcceptsCombinedSkinStreams"
    ]
  },
  "module": "devtools",
  "modules": [
    "common",
    "devtools",
    "mouse",
    "npc_ai",
    "orders",
    "physics_cloth",
    "policyregistry",
    "procgen",
    "procgen_animation",
    "production",
    "profiler",
    "rpg",
    "ui"
  ],
  "origins": [
    "mcp.cpp",
    "mcp_agent_export.cpp",
    "mcp_hot_reload.cpp",
    "mcp_scene_literal.cpp",
    "mcp_soft_restart.cpp",
    "model.cpp",
    "module_lazy_bind.cpp",
    "mouse.cpp",
    "npc_ai_script.cpp",
    "orders.cpp",
    "pcg_biome_controller.cpp",
    "pcg_color_preview_sync.cpp",
    "pcg_terrain_watcher.cpp",
    "physics_cloth.cpp",
    "policyregistry.cpp",
    "procgen_module_assembly.cpp",
    "procgen_runtime_generation.cpp",
    "procgen_scene_sink.cpp",
    "production.cpp",
    "profiler.cpp",
    "reload_script.cpp",
    "rpg_classic_semantic_index.cpp",
    "rpg_dnut_script.cpp",
    "rpg_script.cpp",
    "rpg_world_state_script.cpp",
    "simplesquirrel.cpp"
  ],
  "generated": [
    "Mouse."
  ]
}
*/
// clang-format on

#include "physics/Physics.h"
#include "procgen/Procgen.h"
#include "ui/UI.h"
#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp)
#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include <thread>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "common/ProcgenQuery.h"
#include "common/RenderCapture.h"
#include "common/Runtime.h"
#include "common/SceneQuery.h"
#include "common/ScriptCompiler.h"
#include "devtools/AiPanel.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#include "editor/Editor.h"
#include "pixelworld/PixelWorldModule.h"
#include "ui/EditorHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp.cpp
namespace test_mcp {
using namespace eve::dev;

namespace {

class ClosedLoopCapture final : public eve::IRenderCapture {
public:
    eve::RenderStatusInfo              status() const override { return {}; }
    void                               setReadbackEnabled(bool) override {}
    eve::Result<eve::CaptureImageInfo> savePng(const std::string&) override {
        return eve::Result<eve::CaptureImageInfo>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "ClosedLoopCapture has no framebuffer", "capture"));
    }
    std::string capturePngDataUrl() override { return {}; }
    bool        ensureCamera() override { return false; }
    bool        setCameraPose(float, float, float, float, float, float, float) override { return false; }
    bool        setCameraPoseYawPitch(float, float, float, float, float, float) override { return false; }
    std::string cameraPoseJson() override { return "{}"; }
    eve::Result<eve::Renderable3DInfo> inspectRenderable3D(std::uint32_t entityId, std::uint32_t generation) override {
        if (entityId != info.entityId || generation != info.generation)
            return eve::Result<eve::Renderable3DInfo>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "Renderable3D handle is missing or stale", "entity"));
        return eve::Result<eve::Renderable3DInfo>::success(info);
    }
    std::string visibleEntitiesJson(float, bool*) override { return "{}"; }
    std::string visibleEntitiesJsonAt(float, float, float, float, float, float, float, bool*) override { return "{}"; }
    std::string entityIdMaskJson(int*, int*, bool*) override { return "{}"; }
    eve::Result<void> entityIdMaskPng(const std::string&) override {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "ClosedLoopCapture has no id mask", "id_mask"));
    }
    eve::Result<void> gbufferPng(const std::string&, const std::string&) override {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "ClosedLoopCapture has no gbuffer", "gbuffer"));
    }

    eve::Renderable3DInfo info{42, 7, 1.f, 2.f, 3.f, 0.1f, 0.2f, 0.3f, 1.f};
};

class ClosedLoopEditor final : public eve::IEditorAutomation {
public:
    explicit ClosedLoopEditor(ClosedLoopCapture& capture) : capture_(&capture) {}

    std::string invoke(const std::string& operation, const std::string&) override {
        if (operation == "execute") {
            ++executeCount;
            if (rejectNext) {
                rejectNext = false;
                return R"({"status":"rejected","accepted":false,"diagnostics":[{"rule":"test.reject","message":"rejected"}]})";
            }
            capture_->info.tintG = 0.8f;
            return R"({"status":"applied","accepted":true,"diagnostics":[],"transactionId":"closed-loop.1"})";
        }
        if (operation == "inspect")
            return R"({"status":"applied","accepted":true,"diagnostics":[],"target":{"snapshot":{"revision":2}}})";
        return R"({"status":"unsupported","accepted":false,"diagnostics":[]})";
    }

    int  executeCount = 0;
    bool rejectNext   = true;

private:
    ClosedLoopCapture* capture_ = nullptr;
};

class ClosedLoopScene final : public eve::ISceneQuery {
public:
    std::string activeHost() const override { return "world"; }
    int         hostCount() const override { return 1; }
    std::string hostNameAt(int index) const override { return index == 0 ? "world" : std::string{}; }
    int         nodeCount() const override { return 1; }
    std::string rootId() const override { return "player"; }
    std::vector<eve::SceneNodeInfo> nodes(int) const override { return {node}; }
    std::vector<eve::SceneNodeInfo> nodesOf(const std::string& host, int) const override {
        return host == "world" ? std::vector<eve::SceneNodeInfo>{node} : std::vector<eve::SceneNodeInfo>{};
    }
    eve::Result<eve::SceneNodeInfo> getNode(const std::string& id) const override { return getNodeIn("world", id); }
    eve::Result<eve::SceneNodeInfo> getNodeIn(const std::string& host, const std::string& id) const override {
        if (host != "world" || id != node.id) {
            return eve::Result<eve::SceneNodeInfo>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "scene node not found", "node"));
        }
        return eve::Result<eve::SceneNodeInfo>::success(node);
    }
    eve::Result<void> setNodeTransform(const std::string& id, float x, float y, float z) override {
        if (id != node.id) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "scene node not found", "node"));
        }
        node.x = x;
        node.y = y;
        node.z = z;
        return eve::Result<void>::success();
    }
    eve::Result<void> setNodeVisible(const std::string& id, bool visible) override {
        if (id != node.id) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "scene node not found", "node"));
        }
        node.visible = visible;
        return eve::Result<void>::success();
    }
    void syncTransforms() override {}

    eve::SceneNodeInfo node{"player", "Player", "root/player", true, 1.f, 2.f, 3.f};
};

class ClosedLoopSceneEditor final : public eve::IEditorAutomation {
public:
    explicit ClosedLoopSceneEditor(ClosedLoopScene& scene) : scene_(&scene) {}

    std::string invoke(const std::string& operation, const std::string&) override {
        if (operation == "execute") {
            ++executeCount;
            scene_->node.x = 4.f;
            scene_->node.y = 5.f;
            return R"({"status":"applied","accepted":true,"diagnostics":[],"transactionId":"scene-loop.1"})";
        }
        if (operation == "inspect")
            return R"({"status":"applied","accepted":true,"diagnostics":[],"target":{"snapshot":{"revision":2}}})";
        return R"({"status":"unsupported","accepted":false,"diagnostics":[]})";
    }

    int executeCount = 0;

private:
    ClosedLoopScene* scene_ = nullptr;
};

template <class Interface>
class CapabilityOverride final {
public:
    explicit CapabilityOverride(Interface& replacement)
        : previous_(eve::cap::query<Interface>()), replacement_(&replacement) {
        eve::cap::provide<Interface>(replacement_);
    }
    ~CapabilityOverride() {
        eve::cap::revoke<Interface>(replacement_);
        if (previous_) eve::cap::provide<Interface>(previous_);
    }

private:
    Interface* previous_    = nullptr;
    Interface* replacement_ = nullptr;
};

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
    eve::procgen::Procgen procgenModule;
    eve::physics::Physics physicsModule;
    auto& mcp = McpServer::instance();
    auto& dt  = DevTool::instance();
    auto& ai  = AiPanel::instance();

    mcp.stop();
    ai.clearLog();
    dt.detach();

    ssq::VM vm(1024, ssq::Libs::ALL);
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);

    eve::pixelworld::PixelWorldModule pixelworldModule;
    eve::pixelworld::PixelWorld       pixelworld(9081);

    eve::editor::Editor            editor;
    eve::editor::CommandDescriptor editorCommand;
    editorCommand.id          = eve::editor::CommandId("mcp.test-command");
    editorCommand.ownerModule = "mcp.test";
    editorCommand.displayName = "MCP test command";
    REQUIRE(editor.commandService()
                .registerCommand(std::move(editorCommand),
                                 [](const eve::editor::CommandContext&, const eve::editor::EditorValue&) {
                                     return eve::editing::applied<eve::editor::EditorValue>(
                                         eve::editor::EditorValue("executed"));
                                 })
                .ok());

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
    bool foundStatus           = false;
    bool foundGameplay         = false;
    bool foundEval             = false;
    bool foundScene            = false;
    bool foundProc             = false;
    bool foundPhys             = false;
    bool foundShot             = false;
    bool foundParticles        = false;
    bool foundAudio            = false;
    bool foundVision           = false;
    bool foundVcfg             = false;
    bool foundSdMod            = false;
    bool foundSdBatch          = false;
    bool foundSdInfo           = false;
    bool foundCamGen           = false;
    bool foundReset            = false;
    bool foundSdStat           = false;
    bool foundSdInst           = false;
    bool foundWhitebox         = false;
    bool foundRecipeList       = false;
    bool foundRecipeApply      = false;
    bool foundWorkshop         = false;
    bool foundUiTree           = false;
    bool foundUiGet            = false;
    bool foundUiClick          = false;
    bool foundHostReload       = false;
    bool foundHotReloadStatus  = false;
    bool foundEditorCommands   = false;
    bool foundEditorPlan       = false;
    bool foundEditorCreate     = false;
    bool foundEditorInspect    = false;
    bool foundEditorObserve    = false;
    bool foundObserveStart     = false;
    bool foundObservePoll      = false;
    bool foundObserveClose     = false;
    bool foundRenderableGet    = false;
    bool foundSkeletonInspect  = false;
    bool foundAgentStart       = false;
    bool foundAgentEvidence    = false;
    bool foundAgentComplete    = false;
    bool foundPixelWorlds      = false;
    bool foundPixelSamples     = false;
    bool foundPixelCatalog     = false;
    bool foundPixelValidate    = false;
    bool foundPixelApply       = false;
    bool foundScriptComponents = false;
    bool foundApiSearch        = false;
    bool foundApiGet           = false;
    for (size_t i = 0; i < tools->size(); ++i) {
        auto              t    = tools->getObject(static_cast<unsigned>(i));
        const std::string name = t->getValue<std::string>("name");
        if (name == "eve_status") foundStatus = true;
        if (name == "eve_script_components") foundScriptComponents = true;
        if (name == "eve_gameplay") foundGameplay = true;
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
        if (name == "eve_scene_batch") foundSdBatch = true;
        if (name == "eve_scene_info") foundSdInfo = true;
        if (name == "eve_camera_generate") foundCamGen = true;
        if (name == "eve_scene_reset") foundReset = true;
        if (name == "eve_scene_director_status") foundSdStat = true;
        if (name == "eve_scene_director_install") foundSdInst = true;
        if (name == "eve_whitebox_playtest") foundWhitebox = true;
        if (name == "eve_recipe_list") foundRecipeList = true;
        if (name == "eve_recipe_apply") foundRecipeApply = true;
        if (name == "eve_workshop") foundWorkshop = true;
        if (name == "eve_ui_tree") foundUiTree = true;
        if (name == "eve_ui_get") foundUiGet = true;
        if (name == "eve_ui_click") foundUiClick = true;
        if (name == "eve_host_resource_reload") foundHostReload = true;
        if (name == "eve_host_hot_reload_status") foundHotReloadStatus = true;
        if (name == "eve_editor_commands") foundEditorCommands = true;
        if (name == "eve_editor_plan") foundEditorPlan = true;
        if (name == "eve_editor_target_create") foundEditorCreate = true;
        if (name == "eve_editor_inspect") foundEditorInspect = true;
        if (name == "eve_editor_execute_observe") foundEditorObserve = true;
        if (name == "eve_editor_observe_start") foundObserveStart = true;
        if (name == "eve_editor_observe_poll") foundObservePoll = true;
        if (name == "eve_editor_observe_close") foundObserveClose = true;
        if (name == "eve_renderable3d_get") foundRenderableGet = true;
        if (name == "eve_skeleton_inspect") foundSkeletonInspect = true;
        if (name == "eve_agent_session_start") foundAgentStart = true;
        if (name == "eve_agent_session_evidence") foundAgentEvidence = true;
        if (name == "eve_agent_session_complete") foundAgentComplete = true;
        if (name == "eve_pixelworld_worlds") foundPixelWorlds = true;
        if (name == "eve_pixelworld_samples") foundPixelSamples = true;
        if (name == "eve_pixelworld_catalog_builtin") foundPixelCatalog = true;
        if (name == "eve_pixelworld_catalog_validate") foundPixelValidate = true;
        if (name == "eve_pixelworld_catalog_apply") foundPixelApply = true;
        if (name == "eve_api_search") foundApiSearch = true;
        if (name == "eve_api_get") foundApiGet = true;
    }
    CHECK(foundStatus);
    CHECK(foundGameplay);
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
    CHECK(foundSdBatch);
    CHECK(foundSdInfo);
    CHECK(foundCamGen);
    CHECK(foundReset);
    CHECK(foundSdStat);
    CHECK(foundSdInst);
    CHECK(foundWhitebox);
    CHECK(foundRecipeList);
    CHECK(foundRecipeApply);
    CHECK(foundWorkshop);
    CHECK(foundUiTree);
    CHECK(foundUiGet);
    CHECK(foundUiClick);
    CHECK(foundHostReload);
    CHECK(foundHotReloadStatus);
    CHECK(foundEditorCommands);
    CHECK(foundEditorPlan);
    CHECK(foundEditorCreate);
    CHECK(foundEditorInspect);
    CHECK(foundEditorObserve);
    CHECK(foundObserveStart);
    CHECK(foundObservePoll);
    CHECK(foundObserveClose);
    CHECK(foundRenderableGet);
    CHECK(foundSkeletonInspect);
    CHECK(foundAgentStart);
    CHECK(foundAgentEvidence);
    CHECK(foundAgentComplete);
    CHECK(foundPixelWorlds);
    CHECK(foundPixelSamples);
    CHECK(foundPixelCatalog);
    CHECK(foundPixelValidate);
    CHECK(foundPixelApply);
    CHECK(foundScriptComponents);

    // 脚本组件数据的工具视图：没有脚本 VM/没有登记组件时也必须是合法 JSON（空列表），不是错误。
    client.sendRequest(4, "tools/call", "{\"name\":\"eve_script_components\",\"arguments\":{}}");
    auto scriptComponentsMsg = client.expectResult(4);
    REQUIRE(scriptComponentsMsg);
    auto scriptComponentsContent = scriptComponentsMsg->getObject("result")->getArray("content");
    REQUIRE(scriptComponentsContent);
    REQUIRE(scriptComponentsContent->size() >= 1);
    const std::string scriptComponentsText = scriptComponentsContent->getObject(0)->getValue<std::string>("text");
    CHECK(scriptComponentsText.find("\"entities\"") != std::string::npos);
    CHECK(foundApiSearch);
    CHECK(foundApiGet);

    client.sendRequest(3, "tools/call", "{\"name\":\"eve_status\",\"arguments\":{}}");
    auto statusMsg = client.expectResult(3);
    REQUIRE(statusMsg);
    auto content = statusMsg->getObject("result")->getArray("content");
    REQUIRE(content);
    REQUIRE(content->size() >= 1);
    const std::string text = content->getObject(0)->getValue<std::string>("text");
    CHECK(text.find("\"attached\":true") != std::string::npos);
    CHECK(text.find("\"mcpPort\":") != std::string::npos);

    client.sendRequest(41, "tools/call",
                       "{\"name\":\"eve_api_search\",\"arguments\":{\"query\":\"newWorld\",\"limit\":8}}");
    auto apiSearchMsg = client.expectResult(41);
    REQUIRE(apiSearchMsg);
    const std::string apiSearchText =
        apiSearchMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>("text");
    CHECK(apiSearchText.find("\"ok\":true") != std::string::npos);
    CHECK(apiSearchText.find("\"method\":\"newWorld\"") != std::string::npos);

    client.sendRequest(40, "tools/call",
                       "{\"name\":\"eve_gameplay\",\"arguments\":{\"request\":{\"schemaId\":"
                       "\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"domains\"}}}");
    auto gameplayMsg = client.expectResult(40);
    REQUIRE(gameplayMsg);
    auto gameplayContent = gameplayMsg->getObject("result")->getArray("content");
    REQUIRE(gameplayContent);
    CHECK(gameplayContent->getObject(0)->getValue<std::string>("text").find("\"domains\"") != std::string::npos);

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

    // A plain VM defers authoring-kit installation to the next update.
    auto checkDeferred = [&](const std::string& text) {
        Poco::JSON::Parser parser;
        auto               result = parser.parse(text).extract<Poco::JSON::Object::Ptr>();
        CHECK(!result->getValue<bool>("ok"));
        CHECK(result->getValue<bool>("retryable"));
        CHECK_EQ(result->getValue<std::string>("reason"), std::string("deferred-to-update"));
    };
    const std::string sdStat = textOf(11, "{\"name\":\"eve_scene_director_status\",\"arguments\":{}}");
    checkDeferred(sdStat);

    const std::string resetText = textOf(12, "{\"name\":\"eve_scene_reset\",\"arguments\":{}}");
    checkDeferred(resetText);

    const std::string infoText = textOf(13, "{\"name\":\"eve_scene_info\",\"arguments\":{}}");
    checkDeferred(infoText);

    const std::string camText = textOf(14, "{\"name\":\"eve_camera_generate\",\"arguments\":{\"count\":2}}");
    checkDeferred(camText);

    const std::string modInfo = textOf(15, "{\"name\":\"eve_scene_modify\",\"arguments\":{\"action\":\"info\"}}");
    checkDeferred(modInfo);
    // An unknown action is also deferred until installation completes.
    const std::string modBad = textOf(16, "{\"name\":\"eve_scene_modify\",\"arguments\":{\"action\":\"no_such\"}}");
    checkDeferred(modBad);
    const std::string editorCommands = textOf(17, "{\"name\":\"eve_editor_commands\",\"arguments\":{}}");
    CHECK(editorCommands.find("mcp.test-command") != std::string::npos);
    const std::string editorExecuted =
        textOf(18, "{\"name\":\"eve_editor_execute\",\"arguments\":{\"command\":\"mcp.test-command\",\"payload\":{}}}");
    CHECK(editorExecuted.find("\"status\":\"applied\"") != std::string::npos);
    const std::string renderableAbsent =
        textOf(19, "{\"name\":\"eve_renderable3d_get\",\"arguments\":{\"entityId\":0,\"generation\":1}}");
    CHECK(renderableAbsent.find("graphics module not available") != std::string::npos);
    const std::string observeAbsent =
        textOf(20,
               "{\"name\":\"eve_editor_execute_observe\",\"arguments\":{\"target\":\"mcp.test\",\"command\":\"mcp.test-"
               "command\",\"payload\":{},\"entityId\":0,\"generation\":1}}");
    CHECK(observeAbsent.find("graphics module not available") != std::string::npos);
    const std::string sceneObserveAbsent =
        textOf(21,
               "{\"name\":\"eve_editor_execute_observe\",\"arguments\":{\"target\":\"mcp.test\",\"command\":\"mcp.test-"
               "command\",\"payload\":{},\"observer\":\"scene-node\",\"host\":\"world\",\"node\":\"player\"}}");
    CHECK(sceneObserveAbsent.find("scene module not available") != std::string::npos);
    const std::string pixelWorlds = textOf(22, "{\"name\":\"eve_pixelworld_worlds\",\"arguments\":{}}");
    CHECK(pixelWorlds.find("\"seed\":9081") != std::string::npos);
    const std::string worldId = std::to_string(pixelworld.worldLink().world);
    const std::string pixelPaused =
        textOf(23, "{\"name\":\"eve_pixelworld_pause\",\"arguments\":{\"world\":" + worldId + ",\"paused\":true}}");
    CHECK(pixelPaused.find("\"paused\":true") != std::string::npos);
    const std::string pixelStepped =
        textOf(24, "{\"name\":\"eve_pixelworld_step\",\"arguments\":{\"world\":" + worldId + ",\"count\":2}}");
    CHECK(pixelStepped.find("\"lastTick\":2") != std::string::npos);
    const std::string pixelSamples =
        textOf(25, "{\"name\":\"eve_pixelworld_samples\",\"arguments\":{\"world\":" + worldId + "}}");
    CHECK(pixelSamples.find("\"tick\":2") != std::string::npos);
    const std::string pixelCatalog = textOf(26, "{\"name\":\"eve_pixelworld_catalog_builtin\",\"arguments\":{}}");
    CHECK(pixelCatalog.find("eve.pixelworld.material-catalog") != std::string::npos);
    const std::string invalidCatalog =
        textOf(27, "{\"name\":\"eve_pixelworld_catalog_validate\",\"arguments\":{\"catalog\":{}}}");
    CHECK(invalidCatalog.find("\"ok\":false") != std::string::npos);

    mcp.stop();
    dt.detach();
}

TEST_CASE("devtools.procgenQuery.generateMapReportsStructuredFailure") {
    class AbsentProcgen final : public eve::IProcgenQuery {
    public:
        std::vector<std::string> algorithms() override { return {}; }
        std::vector<std::string> meshRecipes() override { return {}; }
        std::vector<std::string> textureRecipes() override { return {}; }
        std::vector<std::string> pbrRecipes() override { return {}; }
        eve::Result<std::string> generateMap(const std::string&, int, int, uint32_t,
                                             const std::vector<std::pair<std::string, std::string>>&) override {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "Procgen module not available", "procgen"));
        }
        eve::Result<std::string> buildMesh(const std::string&, uint32_t, int, int, int) override {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "Procgen module not available", "procgen"));
        }
    } query;

    auto map = query.generateMap("bsp", 8, 8, 1, {});
    REQUIRE(!map.ok());
    REQUIRE(map.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(map.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);

    auto mesh = query.buildMesh("mesh.fence", 1, -1, -1, -1);
    REQUIRE(!mesh.ok());
    CHECK_EQ(mesh.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("devtools.sceneQuery.getSetReportStructuredFailure") {
    ClosedLoopScene scene;
    auto            missing = scene.getNode("missing");
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(missing.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);

    auto badSet = scene.setNodeTransform("missing", 0.f, 0.f, 0.f);
    REQUIRE(!badSet.ok());
    REQUIRE(badSet.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(badSet.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);

    auto ok = scene.getNode("player");
    REQUIRE(ok.ok());
    CHECK_EQ(std::move(ok).value().id, "player");
    auto visible = scene.setNodeVisible("player", false);
    REQUIRE(visible.ok());
    CHECK(!scene.node.visible);
}

TEST_CASE("devtools.renderCapture.savePngReportsStructuredFailure") {
    ClosedLoopCapture capture;
    auto              saved = capture.savePng("unused.png");
    REQUIRE(!saved.ok());
    REQUIRE(saved.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(saved.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    CHECK_EQ(saved.status().primaryDiagnostic()->path(), "capture");

    auto mask = capture.entityIdMaskPng("unused_id.png");
    REQUIRE(!mask.ok());
    REQUIRE(mask.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(mask.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);

    auto gbuf = capture.gbufferPng("depth", "unused_depth.png");
    REQUIRE(!gbuf.ok());
    REQUIRE(gbuf.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(gbuf.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("devtools.mcp.executeObserveCorrelatesEditorTransactionWithLiveRuntime") {
    auto& mcp = McpServer::instance();
    mcp.stop();

    ClosedLoopCapture                          capture;
    ClosedLoopEditor                           editor(capture);
    CapabilityOverride<eve::IRenderCapture>    captureOverride(capture);
    CapabilityOverride<eve::IEditorAutomation> editorOverride(editor);

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"closed-loop-test\"}}");
    REQUIRE(client.expectResult(1));
    client.sendNotification("notifications/initialized");

    const auto call = [&](int id, int generation, const std::string& expectation) {
        client.sendRequest(id, "tools/call",
                           "{\"name\":\"eve_editor_execute_observe\",\"arguments\":{\"target\":\"material.live\","
                           "\"command\":\"material.property.set.v1\",\"payload\":{\"path\":\"shading.tint\","
                           "\"value\":[0.1,0.8,0.3,1]},\"entityId\":42,\"generation\":" +
                               std::to_string(generation) + ",\"expect\":" + expectation + ",\"tolerance\":0.001}}");
        auto message = client.expectResult(id);
        REQUIRE(message);
        auto content = message->getObject("result")->getArray("content");
        REQUIRE(content);
        return content->getObject(0)->getValue<std::string>("text");
    };

    const std::string stale = call(2, 8, "{\"tint\":[0.1,0.8,0.3,1]}");
    CHECK(stale.find("\"status\":\"stale\"") != std::string::npos);
    CHECK_EQ(editor.executeCount, 0);
    CHECK_EQ(capture.info.tintG, 0.2f);

    const std::string invalid = call(3, 7, "{\"missing\":1}");
    CHECK(invalid.find("\"status\":\"invalid-expectation\"") != std::string::npos);
    CHECK(invalid.find("Observed value has no field missing") != std::string::npos);
    CHECK_EQ(editor.executeCount, 0);
    CHECK_EQ(capture.info.tintG, 0.2f);

    const std::string rejected = call(4, 7, "{\"tint\":[0.1,0.8,0.3,1]}");
    CHECK(rejected.find("\"status\":\"transaction-rejected\"") != std::string::npos);
    CHECK(rejected.find("\"converged\":false") != std::string::npos);
    CHECK(rejected.find("tint[1]") != std::string::npos);
    CHECK(rejected.find("\"tint\":[0.1,0.2,0.3,1]") != std::string::npos);
    CHECK_EQ(editor.executeCount, 1);
    CHECK_EQ(capture.info.tintG, 0.2f);

    const std::string observed = call(5, 7, "{\"tint\":[0.1,0.8,0.3,1]}");
    CHECK(observed.find("\"status\":\"observed\"") != std::string::npos);
    CHECK(observed.find("\"converged\":true") != std::string::npos);
    CHECK(observed.find("\"maxError\":0") != std::string::npos);
    CHECK(observed.find("\"transactionId\":\"closed-loop.1\"") != std::string::npos);
    CHECK(observed.find("\"tint\":[0.1,0.2,0.3,1]") != std::string::npos);
    CHECK(observed.find("\"tint\":[0.1,0.8,0.3,1]") != std::string::npos);
    CHECK_EQ(editor.executeCount, 2);

    mcp.stop();
}

TEST_CASE("devtools.mcp.executeObserveCorrelatesSceneEditorWithNamedHost") {
    auto& mcp = McpServer::instance();
    mcp.stop();

    ClosedLoopScene                            scene;
    ClosedLoopSceneEditor                      editor(scene);
    CapabilityOverride<eve::ISceneQuery>       sceneOverride(scene);
    CapabilityOverride<eve::IEditorAutomation> editorOverride(editor);

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"scene-loop-test\"}}");
    REQUIRE(client.expectResult(1));
    client.sendNotification("notifications/initialized");

    const auto call = [&](int id, const std::string& node) {
        client.sendRequest(id, "tools/call",
                           "{\"name\":\"eve_editor_execute_observe\",\"arguments\":{\"target\":\"scene.live\","
                           "\"command\":\"scene.transform.set.v1\",\"payload\":{\"object\":\"player\","
                           "\"position\":[4,5,3]},\"observer\":\"scene-node\",\"host\":\"world\",\"node\":\"" +
                               node + "\",\"expect\":{\"x\":4,\"y\":5},\"tolerance\":0.001}}");
        auto message = client.expectResult(id);
        REQUIRE(message);
        auto content = message->getObject("result")->getArray("content");
        REQUIRE(content);
        return content->getObject(0)->getValue<std::string>("text");
    };

    const std::string missing = call(2, "missing");
    CHECK(missing.find("\"status\":\"not-found\"") != std::string::npos);
    CHECK_EQ(editor.executeCount, 0);
    CHECK_EQ(scene.node.x, 1.f);

    const std::string observed = call(3, "player");
    CHECK(observed.find("\"status\":\"observed\"") != std::string::npos);
    CHECK(observed.find("\"converged\":true") != std::string::npos);
    CHECK(observed.find("\"transactionId\":\"scene-loop.1\"") != std::string::npos);
    CHECK(observed.find("\"x\":1") != std::string::npos);
    CHECK(observed.find("\"x\":4") != std::string::npos);
    CHECK_EQ(editor.executeCount, 1);
    CHECK_EQ(scene.node.y, 5.f);

    mcp.stop();
}

TEST_CASE("devtools.mcp.rxObservationSessionTracksChangesDeduplicatesAndCloses") {
    auto& mcp = McpServer::instance();
    mcp.stop();

    ClosedLoopScene                      scene;
    CapabilityOverride<eve::ISceneQuery> sceneOverride(scene);
    eve::editor::Editor                  editor;

    const int port = mcp.listen(0);
    REQUIRE(port > 0);
    McpClient client(port);
    client.sendRequest(1, "initialize",
                       "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
                       "\"clientInfo\":{\"name\":\"rx-observation-test\"}}");
    REQUIRE(client.expectResult(1));
    client.sendNotification("notifications/initialized");

    const auto call = [&](int id, const std::string& arguments) {
        client.sendRequest(id, "tools/call", arguments);
        auto message = client.expectResult(id);
        REQUIRE(message);
        auto content = message->getObject("result")->getArray("content");
        REQUIRE(content);
        return content->getObject(0)->getValue<std::string>("text");
    };

    const std::string started = call(
        2,
        R"({"name":"eve_editor_observe_start","arguments":{"observer":"scene-node","host":"world","node":"player","expect":{"x":4},"tolerance":0.001}})");
    Poco::JSON::Parser parser;
    auto               startedObject = parser.parse(started).extract<Poco::JSON::Object::Ptr>();
    REQUIRE(startedObject);
    const std::string sessionId = startedObject->getValue<std::string>("sessionId");
    CHECK(started.find("\"converged\":false") != std::string::npos);

    const std::string request =
        "{\"name\":\"eve_editor_observe_poll\",\"arguments\":{\"sessionId\":\"" + sessionId + "\"}}";
    const std::string initial = call(3, request);
    CHECK(initial.find("\"events\":[]") != std::string::npos);
    const std::string duplicate = call(4, request);
    CHECK(duplicate.find("\"events\":[]") != std::string::npos);

    scene.node.x              = 4.f;
    const std::string changed = call(5, request);
    CHECK(changed.find("\"x\":4") != std::string::npos);
    CHECK(changed.find("\"converged\":true") != std::string::npos);

    scene.node.id                 = "gone";
    const std::string unavailable = call(6, request);
    CHECK(unavailable.find("\"status\":\"observation-unavailable\"") != std::string::npos);
    CHECK(unavailable.find("\"status\":\"not-found\"") != std::string::npos);
    CHECK(unavailable.find("\"converged\":false") != std::string::npos);

    const std::string closed =
        call(7, "{\"name\":\"eve_editor_observe_close\",\"arguments\":{\"sessionId\":\"" + sessionId + "\"}}");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterClose = call(8, request);
    CHECK(afterClose.find("not-found") != std::string::npos);

    mcp.stop();
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
    CHECK(!runtime.scriptCompiler().metadata("mcp_snippet.nut").has_value());
    CHECK(!runtime.scriptCompiler().metadata("eval").has_value());
    bool foundScript = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".nut") foundScript = true;
    }
    CHECK(!foundScript);

    client.sendRequest(
        4, "tools/call",
        "{\"name\":\"eve_skeleton_inspect\",\"arguments\":{\"actor\":\"Lyra\",\"bone\":\"handslot.r\"}}");
    auto skeletonMsg = client.expectResult(4);
    REQUIRE(skeletonMsg);
    const std::string skeletonText =
        skeletonMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>("text");
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

TEST_CASE("devtools.mcp.hostEditorBinding") {
    eve::ui::UI uiModule;
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

TEST_CASE("devtools.mcp.setBreakpointWithCondition") {
    auto& mcp = McpServer::instance();
    auto& dt  = DevTool::instance();
    auto& dbg = Debugger::instance();

    mcp.stop();
    dt.detach();
    dbg.clearBreakpoints();

    ssq::VM vm(1024, ssq::Libs::ALL);
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
                       "{\"name\":\"eve_set_breakpoint\",\"arguments\":"
                       "{\"source\":\"mcp.nut\",\"line\":7,\"condition\":\"n == 2\"}}");
    auto setMsg = client.expectResult(2);
    REQUIRE(setMsg);
    const std::string setText =
        setMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>("text");
    CHECK(setText.find("ok id=") != std::string::npos);

    auto bps = dbg.breakpoints();
    REQUIRE(bps.size() == 1u);
    REQUIRE_EQ(bps[0].line, 7);
    REQUIRE_EQ(bps[0].condition, std::string("n == 2"));

    client.sendRequest(3, "tools/call", "{\"name\":\"eve_list_breakpoints\",\"arguments\":{}}");
    auto listMsg = client.expectResult(3);
    REQUIRE(listMsg);
    const std::string listText =
        listMsg->getObject("result")->getArray("content")->getObject(0)->getValue<std::string>("text");
    REQUIRE(listText.find("n == 2") != std::string::npos);

    dbg.clearBreakpoints();
    mcp.stop();
    dt.detach();
}
}  // namespace test_mcp
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_agent_export)
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "PrivateTestScope.h"
#include "common/CrashLog.h"
#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpScriptTools.hpp"
#include "devtools/McpServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_agent_export.cpp
namespace test_mcp_agent_export {
// Agent-facing runtime evidence: engine stderr, crash forensics and the
// project-script → MCP export registry.
//
// These cases pin the three capabilities an unattended agent needs beyond the
// built-in tool table:
//   1. Engine diagnostics (std::cerr / fprintf(stderr)) reach eve_console_read.
//   2. A restarted game can explain why the previous run died (eve_crash_report).
//   3. A project script publishes its own named, schema-carrying tools.


using namespace eve::dev;

namespace {

/** Run one pre-buffered stdio session and return the newline-framed responses. */
std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

std::string initializeRequest(int id) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"initialize\",\"params\":{" +
           "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"export-test\"}}}";
}

std::string callRequest(int id, const std::string& name, const std::string& argumentsJson) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
           name + "\",\"arguments\":" + argumentsJson + "}}";
}

std::string joinRequests(const std::vector<std::string>& requests) {
    std::string out;
    for (const auto& request : requests) {
        out += request;
        out += '\n';
    }
    return out;
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

/** Result object of a tools/call response line, or null when the shape is wrong. */
Poco::JSON::Object::Ptr toolResult(const std::string& line) {
    try {
        auto object = parseObject(line);
        if (!object || !object->has("result")) return {};
        return object->getObject("result");
    } catch (...) {
        return {};
    }
}

std::string toolText(const std::string& line) {
    auto result = toolResult(line);
    if (!result || !result->has("content")) return {};
    auto content = result->getArray("content");
    if (!content || content->size() == 0) return {};
    auto item = content->getObject(0);
    if (!item || !item->has("text")) return {};
    return item->get("text").convert<std::string>();
}

bool toolIsError(const std::string& line) {
    auto result = toolResult(line);
    return result && result->optValue<bool>("isError", false);
}

/** Call one tool over a fresh stdio session and return its response line. */
std::string callToolOnce(const std::string& name, const std::string& argumentsJson) {
    const std::vector<std::string> responses =
        runStdioSession(joinRequests({initializeRequest(1), callRequest(2, name, argumentsJson)}), 2);
    if (responses.size() < 2) return {};
    return responses[1];
}

/** Declared tool names from a tools/list response line. */
std::vector<std::string> declaredToolNames(const std::string& line) {
    std::vector<std::string> names;
    try {
        auto object = parseObject(line);
        auto result = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto tools  = result ? result->getArray("tools") : Poco::JSON::Array::Ptr();
        if (!tools) return names;
        for (std::size_t index = 0; index < tools->size(); ++index) {
            auto tool = tools->getObject(static_cast<unsigned>(index));
            if (tool) names.push_back(tool->getValue<std::string>("name"));
        }
    } catch (...) {
    }
    return names;
}

const char* kToolScript =
    "eve.mcp.tool(\"game_npcs\", {\n"
    "  description = \"List live NPCs\",\n"
    "  inputSchema = { type = \"object\", properties = { lane = { type = \"string\" } } },\n"
    "  handler = function(args) {\n"
    "    local lane = (\"lane\" in args) ? args.lane : \"none\";\n"
    "    return { count = 2, lane = lane };\n"
    "  }\n"
    "});\n"
    "eve.mcp.tool(\"game_boom\", {\n"
    "  description = \"Always fails\",\n"
    "  handler = function(args) { throw \"boom\"; }\n"
    "});\n";

}  // namespace

TEST_CASE("devtools.mcp.stderrCaptureFeedsConsole") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    ssq::VM vm(512, ssq::Libs::ALL);
    dt.attach(vm, false);
    CHECK(console.isCapturingStderr());

    // Both engine output sinks must be covered: a streambuf swap would miss the
    // forty-odd fprintf(stderr, ...) call sites.
    std::fprintf(stderr, "engine-fprintf-marker\n");
    std::fflush(stderr);
    std::cerr << "engine-cerr-marker" << std::endl;

    bool sawFprintf = false;
    bool sawCerr    = false;
    for (int attempt = 0; attempt < 300 && !(sawFprintf && sawCerr); ++attempt) {
        const std::string text = console.format(200);
        sawFprintf             = text.find("engine-fprintf-marker") != std::string::npos;
        sawCerr                = text.find("engine-cerr-marker") != std::string::npos;
        if (!(sawFprintf && sawCerr)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(sawFprintf);
    CHECK(sawCerr);

    bool          tagged   = false;
    bool          seqRanUp = false;
    std::uint64_t previous = 0;
    for (const auto& line : console.recent(200)) {
        if (line.level == "engine" && line.text == "engine-cerr-marker") tagged = true;
        if (line.seq <= previous) seqRanUp = true;
        previous = line.seq;
    }
    CHECK(tagged);
    CHECK(!seqRanUp);

    dt.detach();
    CHECK(!console.isCapturingStderr());
    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.engineLineDropsScriptEchoOnly") {
    auto& console = ConsolePanel::instance();
    console.setMaxEntries(500);
    console.clear();

    // capturePrint/captureError forward to stderr, so the descriptor capture
    // sees the same text again. Only that echo is dropped.
    console.addLog("print", "echo-line");
    console.addEngineLine("echo-line");
    console.addLog("error", "echo-error");
    console.addEngineLine("echo-error");
    // A repeat produced by the engine itself is not an echo and must survive.
    console.addEngineLine("engine-only");
    console.addEngineLine("engine-only");

    int echoPrint  = 0;
    int echoError  = 0;
    int engineOnly = 0;
    for (const auto& line : console.recent(50)) {
        if (line.text == "echo-line") ++echoPrint;
        if (line.text == "echo-error") ++echoError;
        if (line.text == "engine-only") ++engineOnly;
    }
    CHECK_EQ(echoPrint, 1);
    CHECK_EQ(echoError, 1);
    CHECK_EQ(engineOnly, 2);

    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.crashReportExplainsPreviousRun") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir   = std::filesystem::temp_directory_path() / ("eve-mcp-crash-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);

    eve::initSystemLogging(dir.string());
    const std::string path = eve::crashLogPath();
    REQUIRE(!path.empty());
    CHECK(path.find("eve.log") != std::string::npos);

    // Session 1 crashes, session 2 is the restart asking the question, session 3
    // would be the next restart after a clean exit.
    eve::recordCrashEvent("[crash] code=0x00000005 at 0x0\n");
    eve::recordSessionStart();

    const std::string beforeLine = callToolOnce("eve_crash_report", "{\"lines\":60}");
    const std::string before     = toolText(beforeLine);
    REQUIRE(!before.empty());
    CHECK(!toolIsError(beforeLine));

    auto summary = parseObject(before);
    REQUIRE(summary);
    CHECK(summary->getValue<bool>("exists"));
    CHECK(summary->getValue<int>("crashes") >= 1);
    CHECK(summary->getValue<int>("sessions") >= 2);
    CHECK(!summary->getValue<std::string>("lastCrashAt").empty());
    CHECK(before.find("[crash] code=0x00000005") != std::string::npos);
    CHECK(before.find("tail") != std::string::npos);
    // The session before the current one did not end: that is the crash verdict.
    CHECK(summary->optValue<bool>("previousSessionCrashed", false));
    CHECK(!summary->optValue<bool>("previousSessionEnded", true));

    // A clean exit writes the end marker, which is how a reader distinguishes
    // "the previous run finished" from "the previous run died".
    eve::recordSessionEnd(0);
    eve::recordSessionStart();
    const std::string after = toolText(callToolOnce("eve_crash_report", "{}"));
    auto              clean = parseObject(after);
    REQUIRE(clean);
    CHECK(clean->optValue<bool>("previousSessionEnded", false));
    CHECK(!clean->optValue<bool>("previousSessionCrashed", true));

    // eve_status points at the same artifact without reading it.
    const std::string status = toolText(callToolOnce("eve_status", "{}"));
    CHECK(status.find(path) != std::string::npos);
    CHECK(status.find("\"crashLog\"") != std::string::npos);
}

TEST_CASE("devtools.mcp.scriptToolExportRoundTrip") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    ssq::VM vm(1024, ssq::Libs::ALL);
    {
        auto bootstrap = vm.compileSource("eve <- {};\n");
        vm.run(bootstrap);
    }
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);
    {
        auto script = vm.compileSource(kToolScript);
        vm.run(script);
    }
    CHECK_EQ(scriptToolCount(), 2);
    CHECK(isScriptTool("game_npcs"));
    CHECK(isScriptTool("game_boom"));

    const std::vector<std::string> listed = runStdioSession(
        joinRequests({initializeRequest(1), "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"}),
        2);
    REQUIRE(listed.size() == 2);
    const std::vector<std::string> names = declaredToolNames(listed[1]);
    REQUIRE(!names.empty());
    CHECK(std::find(names.begin(), names.end(), "game_npcs") != names.end());
    CHECK(std::find(names.begin(), names.end(), "game_boom") != names.end());
    CHECK(listed[1].find("List live NPCs") != std::string::npos);
    CHECK(listed[1].find("\"properties\":{\"lane\":{\"type\":\"string\"}}") != std::string::npos);

    // Anti-shadowing invariant: nothing a project can register may collide with
    // a built-in, because built-ins win at dispatch time.
    std::string shadowable;
    for (const auto& name : names) {
        if (name == "game_npcs" || name == "game_boom") continue;
        if (isValidScriptToolName(name)) shadowable += name + " ";
    }
    CHECK_EQ(shadowable, std::string());

    // Handler arguments arrive as a Squirrel table; the return value is JSON.
    const std::string okLine = callToolOnce("game_npcs", "{\"lane\":\"north\"}");
    CHECK(!toolIsError(okLine));
    auto ok = parseObject(toolText(okLine));
    REQUIRE(ok);
    CHECK_EQ(ok->getValue<int>("count"), 2);
    CHECK_EQ(ok->getValue<std::string>("lane"), std::string("north"));

    // Missing arguments become an empty table rather than an error.
    auto emptyArgs = parseObject(toolText(callToolOnce("game_npcs", "{}")));
    REQUIRE(emptyArgs);
    CHECK_EQ(emptyArgs->getValue<std::string>("lane"), std::string("none"));

    // A throwing handler is reported as a tool error, not a protocol failure.
    const std::string boomLine = callToolOnce("game_boom", "{}");
    CHECK(toolIsError(boomLine));
    CHECK(toolText(boomLine).find("error:") == 0);

    // Unregistering removes it from discovery as well as dispatch.
    {
        auto removeScript = vm.compileSource(
            "local removed = eve.mcp.remove(\"game_npcs\");\n"
            "local left = eve.mcp.tools();\n");
        vm.run(removeScript);
    }
    CHECK(!isScriptTool("game_npcs"));
    CHECK_EQ(scriptToolCount(), 1);
    CHECK(!isScriptTool("eve_status"));  // built-ins are never in this registry
    const std::vector<std::string> afterRemove = runStdioSession(
        joinRequests({initializeRequest(1), "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"}),
        2);
    REQUIRE(afterRemove.size() == 2);
    CHECK(afterRemove[1].find("game_npcs") == std::string::npos);

    // Detaching the VM must not leave orphaned handlers behind.
    dt.detach();
    CHECK_EQ(scriptToolCount(), 0);
    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.scriptToolNameRules") {
    CHECK(isValidScriptToolName("game_npcs"));
    CHECK(isValidScriptToolName("abc"));
    CHECK(!isValidScriptToolName("ab"));             // too short
    CHECK(!isValidScriptToolName("Game"));           // uppercase
    CHECK(!isValidScriptToolName("1game"));          // leading digit
    CHECK(!isValidScriptToolName("game-npcs"));      // punctuation
    CHECK(!isValidScriptToolName("eve_status"));     // built-in prefix
    CHECK(!isValidScriptToolName("eve_custom"));     // built-in prefix
    CHECK(!isValidScriptToolName("inspect_thing"));  // built-in prefix
    CHECK(!isValidScriptToolName("set_thing"));      // built-in prefix
    CHECK(!isValidScriptToolName("capture_thing"));  // built-in prefix
    CHECK(!isValidScriptToolName("get_thing"));      // built-in prefix
    CHECK(!isValidScriptToolName(std::string(65, 'a')));

    // A rejected registration must not enter the registry.
    auto& dt = DevTool::instance();
    dt.detach();
    ssq::VM vm(512, ssq::Libs::ALL);
    {
        auto bootstrap = vm.compileSource("eve <- {};\n");
        vm.run(bootstrap);
    }
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);
    {
        auto script = vm.compileSource(
            "local ok = eve.mcp.tool(\"eve_shadow\", { handler = function(a) { return 1; } });\n"
            "local ok2 = eve.mcp.tool(\"no_handler\", { description = \"x\" });\n");
        vm.run(script);
    }
    CHECK_EQ(scriptToolCount(), 0);
    dt.detach();
}
}  // namespace test_mcp_agent_export
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_hot_reload)
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/ScriptError.h"
#include "common/ServiceInterfaces.h"
#include "common/SquirrelBinding.h"
#include "ui/EditorHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_hot_reload.cpp
namespace test_mcp_hot_reload {
namespace {

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out << text;
    REQUIRE(out.good());
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

}  // namespace

TEST_CASE("devtools.mcp.hostResourceHotReload") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_hot_reload_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto editorPath = root / "editors" / "terrain.editor.json";
    const auto vmPath     = root / "editors" / "terrain.vm.nut";
    const auto mcpPath    = root / "mcp.nut";

    writeText(
        editorPath,
        R"({"id":"terrain","title":"Terrain v1","vm":"TerrainVM","children":[{"type":"slider","id":"brushSize","value":5,"bind":"vm.brushSize","onChange":"vm.onChange"}]})");
    writeText(
        vmPath,
        R"(::TerrainVM <- { version = 1, brushSize = 5, changedCount = 0, onChange = function(widget, value) { this.changedCount++; } };)");
    writeText(mcpPath, R"(::mcpReloadVersion <- 1;)");

    ssq::VM vm(1024, ssq::Libs::ALL);
    host.start(vm, root.string(), /*allowWindow=*/false);

    REQUIRE(host.listEditors().find("terrain") != std::string::npos);
    REQUIRE(host.setEditorValue("terrain", "brushSize", "12") == "ok");
    CHECK(vm.find("TerrainVM").toTable().find("brushSize").toFloat() == 12.f);
    CHECK(vm.find("TerrainVM").toTable().find("changedCount").toInt() == 1);
    CHECK(host.consumeEvents("terrain").find("change") != std::string::npos);

    writeText(
        vmPath,
        R"(::TerrainVM <- { version = 2, brushSize = 5, changedCount = 0, onChange = function(widget, value) { this.changedCount++; } };)");
    CHECK(host.reloadResource("editors/terrain.vm.nut") == "ok");
    auto terrainVm = vm.find("TerrainVM").toTable();
    CHECK(terrainVm.find("version").toInt() == 2);
    CHECK(terrainVm.find("brushSize").toFloat() == 12.f);
    CHECK(terrainVm.find("changedCount").toInt() == 0);
    CHECK(host.consumeEvents("terrain") == "[]");

    writeText(vmPath, "::TerrainVM <- { broken = ; };");
    CHECK(host.reloadResource("editors/terrain.vm.nut").rfind("error:", 0) == 0);
    terrainVm = vm.find("TerrainVM").toTable();
    CHECK(terrainVm.find("version").toInt() == 2);
    CHECK(terrainVm.find("brushSize").toFloat() == 12.f);

    writeText(
        editorPath,
        R"({"id":"terrain","title":"Terrain v2","vm":"TerrainVM","children":[{"type":"slider","id":"brushSize","value":3,"bind":"vm.brushSize","onChange":"vm.onChange"},{"type":"text","id":"status","value":"ready"}]})");
    CHECK(host.reloadResource("editors/terrain.editor.json").rfind("error:", 0) != 0);
    CHECK(host.listEditors().find("Terrain v2") != std::string::npos);
    CHECK(host.editorState("terrain").find("\"brushSize\":12") != std::string::npos);
    CHECK(host.consumeEvents("terrain") == "[]");

    writeText(editorPath, R"({"id":"other","title":"Wrong id","children":[]})");
    CHECK(host.reloadResource("editors/terrain.editor.json").find("must match") != std::string::npos);
    CHECK(host.listEditors().find("Terrain v2") != std::string::npos);

    CHECK(host.reloadResource("mcp.nut") == "ok");
    CHECK(vm.find("mcpReloadVersion").toInt() == 1);
    CHECK(host.reloadResource("../outside.nut").find("outside project root") != std::string::npos);

    host.setHotReloadWatchCount(1);
    const auto status = parseObject(host.hotReloadStatus());
    REQUIRE(status);
    CHECK(status->getValue<bool>("enabled"));
    CHECK(status->getValue<int>("watchCount") == 1);
    CHECK(status->getValue<int>("reloadCount") >= 3);
    CHECK(status->getValue<int>("failureCount") >= 2);

    host.stop();
    std::filesystem::remove_all(root);
}

TEST_CASE("devtools.mcp.hostScriptFailureCarriesTheReason") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_host_text_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto vmPath = root / "editors" / "terrain.vm.nut";
    std::filesystem::create_directories(root / "editors");

    writeText(vmPath, R"(::TerrainVM <- { version = 1 };)");
    writeText(
        root / "editors" / "terrain.editor.json",
        R"({"id":"terrain","title":"Terrain","vm":"TerrainVM","children":[{"type":"slider","id":"brushSize","value":5,"bind":"vm.brushSize"}]})");

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::script::installScriptErrorHook(vm.getHandle());
    host.start(vm, root.string(), /*allowWindow=*/false);
    REQUIRE(host.listEditors().find("terrain") != std::string::npos);

    // ViewModel 的编译失败必须带上出处与位置，而不是只回一个通用词。dev 之后把来源名从
    // 缺省的 "host_vm.nut" 换成真实资源路径（`game:/editors/<id>.vm.nut`），所以断言的是
    // 文件名的后半段而不是旧名字。实测字符串形如
    // "error: compile failed:  error at game:/editors/terrain.vm.nut:1 (<anonymous>): expression expected"。
    writeText(vmPath, "::TerrainVM <- { broken = ; };");
    const std::string compileError = host.reloadResource("editors/terrain.vm.nut");
    REQUIRE(compileError.rfind("error:", 0) == 0);
    REQUIRE(compileError.find("compile failed") != std::string::npos);
    REQUIRE(compileError.find("terrain.vm.nut") != std::string::npos);

    // 运行期抛出必须带上脚本错误原文与行号：
    // "error: runtime failed: game:/editors/terrain.vm.nut:2 (main): vm chunk exploded"。
    writeText(vmPath, "::TerrainVM <- { version = 2 };\nthrow \"vm chunk exploded\";\n");
    const std::string runtimeError = host.reloadResource("editors/terrain.vm.nut");
    REQUIRE(runtimeError.rfind("error:", 0) == 0);
    REQUIRE(runtimeError.find("runtime failed") != std::string::npos);
    REQUIRE(runtimeError.find("terrain.vm.nut:2") != std::string::npos);
    REQUIRE(runtimeError.find("vm chunk exploded") != std::string::npos);

    // 片段路径（mcp.nut）走的是同一个 runSquirrel。
    writeText(root / "mcp.nut", "throw \"snippet exploded\";\n");
    const std::string snippetError = host.reloadResource("mcp.nut");
    REQUIRE(snippetError.find("runtime failed") != std::string::npos);
    REQUIRE(snippetError.find("snippet exploded") != std::string::npos);

    host.stop();
    std::filesystem::remove_all(root);

    // onChange 抛出：`setEditorValue` 仍然回 "ok"（绑定值已经写进 ViewModel），但失败必须
    // 被计数上报——`callTableFunc` 过去把原因压成 "runtime failed: <name>" 并只在 stderr
    // 打一行。用新的项目目录，避免上面那个会抛的 terrain VM 干扰计数。
    const auto root2 =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_onchange_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root2 / "editors");
    writeText(
        root2 / "editors" / "err.editor.json",
        R"({"id":"err","title":"Err","vm":"ErrVM","children":[{"type":"slider","id":"v","value":1,"bind":"vm.v","onChange":"vm.onChange"}]})");
    writeText(root2 / "editors" / "err.vm.nut",
              R"(::ErrVM <- { v = 1, onChange = function(widget, value) { throw "onChange exploded"; } };)");

    ssq::VM vm2(1024, ssq::Libs::ALL);
    eve::script::installScriptErrorHook(vm2.getHandle());
    eve::script::resetReportedScriptCallFailures();
    host.start(vm2, root2.string(), /*allowWindow=*/false);
    REQUIRE(eve::script::reportedScriptCallFailures() == 0);

    REQUIRE(host.setEditorValue("err", "v", "7") == "ok");
    REQUIRE(eve::script::reportedScriptCallFailures() == 1);
    REQUIRE(vm2.find("ErrVM").toTable().find("v").toFloat() == 7.0);

    host.stop();
    std::filesystem::remove_all(root2);
}

TEST_CASE("devtools.mcp.startupViewModelFailureIsVisible") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_startup_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "editors");
    writeText(
        root / "editors" / "broken.editor.json",
        R"({"id":"broken","title":"Broken","vm":"BrokenVM","children":[{"type":"slider","id":"v","value":1,"bind":"vm.v"}]})");
    // The manifest is fine; the ViewModel throws while it is loaded at startup.
    writeText(root / "editors" / "broken.vm.nut", "::BrokenVM <- { v = 1 };\nthrow \"startup vm exploded\";\n");

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::script::installScriptErrorHook(vm.getHandle());
    eve::script::resetReportedScriptCallFailures();
    host.start(vm, root.string(), /*allowWindow=*/false);

    // The editor itself still loads -- only its script failed -- and that failure must be
    // visible through the status an MCP client reads. Before this, the result was
    // discarded and hotReloadStatus() reported a healthy host.
    REQUIRE(host.listEditors().find("broken") != std::string::npos);
    const auto status = parseObject(host.hotReloadStatus());
    REQUIRE(status);
    REQUIRE(status->getValue<int>("failureCount") >= 1);
    const std::string lastResult = status->getValue<std::string>("lastResult");
    REQUIRE(lastResult.rfind("error:", 0) == 0);
    REQUIRE(lastResult.find("startup vm exploded") != std::string::npos);

    host.stop();
    std::filesystem::remove_all(root);
}

namespace {

class MemoryFileSystem final : public eve::service::IFileSystem {
public:
    std::unordered_map<std::string, std::string> files;

    bool readFile(const std::string& path, std::vector<uint8_t>& output) override {
        const auto found = files.find(path);
        if (found == files.end()) return false;
        output.assign(found->second.begin(), found->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return files.find(path) != files.end(); }
};

}  // namespace

TEST_CASE("devtools.mcp.hostScriptImport") {
    auto& host = eve::ui::EditorHost::instance();
    host.stop();

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_mcp_host_import_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    writeText(root / "mcp.nut",
              "import { VALUE } from \"game:/lib/value.nut\"\n"
              "imported_value <- VALUE + 1\n");

    MemoryFileSystem filesystem;
    filesystem.files["lib/value.nut"] = "export const VALUE = 41\n";
    eve::cap::detail::clearAllRaw();
    eve::cap::provide<eve::service::IFileSystem>(&filesystem);

    eve::Runtime runtime(512, ssq::Libs::ALL);
    eve::ModuleManager::expose(runtime);
    host.start(runtime.vm(), root.string(), /*allowWindow=*/false);

    CHECK(host.reloadResource("mcp.nut") == "ok");
    CHECK(runtime.vm().get<int64_t>("imported_value") == int64_t(42));

    filesystem.files["lib/value.nut"] = "export const VALUE = 7\n";
    CHECK(host.reloadResource("mcp.nut") == "ok");
    CHECK(runtime.vm().get<int64_t>("imported_value") == int64_t(8));

    host.stop();
    eve::cap::detail::clearAllRaw();
    std::filesystem::remove_all(root);
}
}  // namespace test_mcp_hot_reload
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_scene_literal)
#include <Poco/JSON/Parser.h>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "devtools/McpSceneLiteral.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_scene_literal.cpp
namespace test_mcp_scene_literal {
TEST_CASE("mcp.scene_authoring.parameters_preserve_nested_values_and_precision") {
    Poco::JSON::Parser parser;
    auto               input =
        parser.parse(R"({"id":"subject","pos":[1.123456789012345,0,0],"visible":false,"nested":{"a b":"quote\""}})");
    auto encoded = eve::dev::sceneLiteral(input);
    REQUIRE(encoded.ok());
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto source = "parameters <- " + encoded.value() + ";";
    vm.run(vm.compileSource(source.c_str()));
    auto parameters = vm.find("parameters").toTable();
    REQUIRE_EQ(parameters.get<std::string>("id"), "subject");
    REQUIRE(!parameters.get<bool>("visible"));
    REQUIRE_EQ(parameters.get<ssq::Array>("pos").get<float>(0), static_cast<float>(1.123456789012345));
    REQUIRE_EQ(parameters.get<ssq::Table>("nested").get<std::string>("a b"), "quote\"");
    REQUIRE(!eve::dev::sceneLiteral(Poco::Dynamic::Var(std::numeric_limits<double>::infinity())).ok());
    REQUIRE(!eve::dev::sceneLiteral(input, 17).ok());
}
}  // namespace test_mcp_scene_literal
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_soft_restart)
#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include <chrono>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include <thread>
#include "PrivateTestScope.h"
#include "devtools/AiPanel.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/SoftRestart.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_soft_restart.cpp
namespace test_mcp_soft_restart {
// Soft restart over MCP: reset game state without dropping the resource cache.
//
// Agents debugging a live game must not kill the process between iterations —
// re-decoding assets is the expensive part. eve_restart keeps ResourceManager
// entries and re-enters soft_restart_game / eve_init with optional args.


namespace {

using eve::dev::AiPanel;
using eve::dev::DevTool;
using eve::dev::executeSoftRestart;
using eve::dev::McpServer;
using eve::dev::resourceCacheCount;
using eve::dev::SoftRestartRequest;

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
    vm.run(vm.compileSource(R"SQ(
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
)SQ"));
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
    REQUIRE(result.ok());
    CHECK(result.value().scriptsReloaded);
    CHECK(result.value().initCalled);
    CHECK_EQ(result.value().resourceCountBefore, before);
    CHECK_EQ(result.value().resourceCountAfter, before);
    CHECK_EQ(vm.find("restart_count").toInt(), 1);
    CHECK_EQ(vm.find("last_seed").toInt(), 42);

    request.args->set("seed", 99);
    request.reloadScripts = false;
    auto again            = executeSoftRestart(vm.getHandle(), std::move(request));
    REQUIRE(again.ok());
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
}  // namespace test_mcp_soft_restart
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_model)
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
// Squirrel internal headers require this dependency order.
// clang-format off
#include "sqobject.h"
#include "sqpcheader.h"
#include "sqtable.h"
#include "sqclass.h"
#include "sqvm.h"
// clang-format on
#include "squirrel.h"
// Squirrel internal headers require this dependency order.
// clang-format off
#include "sqobject.h"
#include "sqpcheader.h"
#include "sqtable.h"
#include "sqclass.h"
#include "sqvm.h"
// clang-format on
extern const char* model_content;
extern const char* model2_content;
// model.cpp
namespace test_model {
using namespace eve;


class ModelScriptTest {
public:
    struct Model {
        std::map<std::string, ssq::Object> models;
        std::vector<ssq::Object>           instances;
        std::vector<ssq::Object>           updates;
    };

    ModelScriptTest() : vm(2048, ssq::Libs::ALL) {
        model = new Model();
        expose(vm);
        ssq::Script s1 = vm.compileSource(model_content);
        ssq::Script s2 = vm.compileSource(model2_content);
        vm.run(s1);
        vm.callFunc(vm.findFunc("update"), vm);
        printf("-----------------------------\n");
        vm.run(s2);
        vm.callFunc(vm.findFunc("update"), vm);
        printf("-----------------------------\n");
    }
    ~ModelScriptTest() { delete model; }

protected:
    Model* model;

    void expose(ssq::VM& vm) {
        ModuleManager::expose(vm);
        auto eve = vm.find("eve").toTable();

        static bool first = true;

        eve.addFunc("isClick", [&]() {
            if (first) {
                first = false;
                return true;
            } else
                return false;
        });

        eve.addFunc("model", [](ssq::Object cls) {
            printf("eve.model(class)\n");
            auto o = cls.getRaw();
            CHECK_EQ(o._type, OT_CLASS);
            auto pclass = o._unVal.pClass;
            auto t      = pclass->_members;
            for (SQInteger i = 0; i < t->_numofnodes; i++) {
                if (sq_type(t->_nodes[i].key) != OT_NULL) {
                    printf("%s ", _string(t->_nodes[i].key)->_val);
                    auto p = t->_nodes[i].val;
                    if (sq_type(p) == OT_INTEGER) {
                        if (_isfield(p)) {
                            printf("field %d\n", static_cast<int>(_member_idx(p)));
                        }
                        if (_ismethod(p)) {
                            printf("method %d\n", static_cast<int>(_member_idx(p)));
                        }
                    }
                }
            }
        });
        eve.addFunc("reg", [&](ssq::Object obj) { model->instances.push_back(obj); });
        eve.addFunc("set", [&](std::string s, ssq::Object obj) { model->models[s] = obj; });
        eve.addFunc("get", [&](std::string s) { return model->models[s]; });
    }

    const char* script;
    ssq::VM     vm;
};


// Release builds: the raw Squirrel-internal introspection inside the "basic"
// script handler crashes (SIGBUS on macOS arm64, SEGFAULT on Windows; Linux
// Release is fine). Pre-existing white-box test issue. Coverage still runs in
// Debug on every platform and in Release on Linux.
#if !defined(NDEBUG)
TEST_CASE_FIXTURE(ModelScriptTest, "ModelScriptTest.basic") { REQUIRE(vm.callFunc(vm.findFunc("basic"), vm).toBool()); }
#endif
}  // namespace test_model
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_module_lazy_bind)
#include <squirrel.h>
#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include "Fixtures.h"
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// module_lazy_bind.cpp
namespace test_module_lazy_bind {
// module_lazy_bind.cpp
using namespace eve;

namespace {

bool eveRawHas(HSQUIRRELVM vm, const char* key) {
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, "eve", -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return false;
    }
    sq_pushstring(vm, key, -1);
    const bool has = SQ_SUCCEEDED(sq_rawget(vm, -2));
    sq_settop(vm, top);
    return has;
}

}  // namespace

TEST_CASE("moduleExpose.defersNativeClassUntilFirstGet") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);
    HSQUIRRELVM vm = runtime.handle();
    REQUIRE(!eveRawHas(vm, "Graphics"));
    REQUIRE(!eveRawHas(vm, "Window"));
    REQUIRE(!eveRawHas(vm, "WindowSettings"));

    runtime.runSource("g <- eve.Graphics\n", "lazy-graphics.nut");
    REQUIRE(eveRawHas(vm, "Graphics"));
    REQUIRE(!eveRawHas(vm, "Window"));

    runtime.runSource("s <- eve.WindowSettings()\n", "lazy-window-settings.nut");
    REQUIRE(eveRawHas(vm, "Window"));
    REQUIRE(eveRawHas(vm, "WindowSettings"));
}

TEST_CASE("moduleExpose.eagerCompatibilityKeepsCanonicalSpriteBinding") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    runtime.runSource("if (eve._bindAllNativeClasses() < 0) throw \"native binding failed\"\n",
                      "eager-native-binding-setup.nut");
    REQUIRE(eveRawHas(runtime.handle(), "_ecsSlotsCache"));
    REQUIRE(eveRawHas(runtime.handle(), "_ecsTypes"));

    runtime.runSource(
        "sprite <- eve.Sprite2D()\n"
        "sprite.setBlend(\"alpha\")\n"
        "sprite.setAnchor(0.5, 0.5)\n"
        "class CompatComponent extends eve.Component { value = 1 }\n"
        "class CompatEntity extends eve.Entity { data = CompatComponent }\n"
        "compatEntity <- CompatEntity.create()\n"
        "if (compatEntity.data.value != 1) throw \"script ECS component creation failed\"\n"
        "if (eve.view(CompatEntity).len() != 1) throw \"script ECS view failed\"\n"
        "class CompatSystem extends eve.System {\n"
        "  function update(dt) { return dt + 1 }\n"
        "}\n"
        "compatSystem <- CompatSystem()\n"
        "if (compatSystem.update(2) != 3) throw \"script ECS System was replaced\"\n"
        "if (\"getPlatform\" in compatSystem) throw \"native System leaked into script ECS\"\n"
        "class CompatEntity extends eve.Entity {\n"
        "  function update(dt) { return dt + 2 }\n"
        "}\n"
        "compatEntity <- CompatEntity()\n"
        "if (compatEntity.update(2) != 4) throw \"script ECS Entity identity changed\"\n"
        "vehicle <- eve.Vehicle()\n"
        "if (!(\"update\" in vehicle)) throw \"Vehicle binding lost update\"\n"
        "scene <- eve.Scene()\n"
        "if (!(\"update\" in scene)) throw \"Scene binding lost update\"\n",
        "eager-native-binding.nut");
}

TEST_CASE("moduleExpose.lazyGraphicsPreservesReturnedSpriteSurface") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    runtime.runSource("g <- eve.Graphics()\n", "lazy-graphics-instance.nut");
    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        s <- g.newSprite2D()
        if (!(s instanceof eve.Sprite2D)) throw "wrong Sprite2D class after pending exposers"
        s.setBlend("alpha")
        s.setAnchor(0.25, 0.75)
        s.destroy()
    )",
                      "lazy-graphics-sprite.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveScriptEcsOverrides") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        class LazyPosition extends eve.Component { x = 0.0 }
        class LazyEntity extends eve.Entity { position = LazyPosition }
        class LazySystem extends eve.System {
            constructor() { base.constructor(LazyEntity) }
            function update(dt) {
                foreach (entity in entities()) entity.position.x += dt
            }
        }
        if (typeof eve._ecsSlotsCache != "table") throw "ECS slot cache was replaced"
        if (!(eve.isSubclass(LazyEntity, eve.Entity))) throw "ECS entity inheritance was lost"
        if (!(eve.isSubclass(LazyPosition, eve.Component))) throw "ECS component inheritance was lost"
        if (!(eve.isSubclass(LazySystem, eve.System))) throw "ECS system inheritance was lost"
        local lazyFields = eve.inspectClassFields(LazyEntity)
        if (typeof lazyFields != "table") throw "ECS field inspection was replaced"
        if (!("position" in lazyFields)) throw "ECS entity field inspection lost position"
        if (lazyFields.position != LazyPosition) throw "ECS entity field inspection changed position"
        if (!(eve._ecsIsComponentClass(lazyFields.position))) throw "ECS component classification failed"
        lazyEntity <- LazyEntity.create()
        lazySystem <- LazySystem()
        lazySystem.update(0.5)
        if (lazyEntity.position.x != 0.5) throw "script ECS override was lost"
        hostOS <- eve.OS()
        if (hostOS.getName() != "OS") throw "OS module binding was lost"
    )",
                      "lazy-script-ecs.nut");
}

TEST_CASE("moduleExpose.osSlotUsesUnambiguousName") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);
    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        function moduleSlotLive(slot) {
            return slot in getroottable() && getroottable()[slot] != null &&
                   typeof getroottable()[slot] == "instance"
        }
        if (moduleSlotLive("os")) throw "OS module slot was unexpectedly occupied"
        os <- eve.OS()
        if (!moduleSlotLive("os")) throw "OS instance was not accepted"
        os.limitFrame()
    )",
                      "os-slot.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveRenderable3DSurface") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    runtime.runSource("g <- eve.Graphics()\n", "lazy-graphics-3d-instance.nut");
    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        renderable <- eve.Renderable3D()
        renderable.clearMeshLod()
        renderable.setPosition(1.0, 2.0, 3.0)
        renderable.setRoughness(0.8)
        renderable.setPackedNormalMask(true)
        renderable.setLayer(8)
        if (renderable.getLayer() != 8) throw "Renderable3D layer binding was lost"
        renderable.setCastShadow(true)
        renderable.setReceiveShadow(true)
    )",
                      "lazy-graphics-3d.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveProcgenRockSurface") {
    eve::graphics::Graphics* graphics = nullptr;
    openHeadlessGfx(graphics, 128, 128);
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        procgen <- eve.Procgen()
        paramsResult <- procgen.newParams()
        if (!paramsResult.ok) throw "Procgen parameter creation failed"
        rockParams <- paramsResult.value
        rockParams.setSeed(1847)
        rockParams.setInt("subdivisions", 3)
        rockParams.setString("baseShape", "mixed")
        rockParams.setFloat("variation", 0.48)
        rockParams.setFloat("radius", 0.72)
        rockParams.setSize(256, 256)
        gfx <- eve.Graphics()
        meshResult <- procgen.generateMesh("mesh.rock", rockParams, gfx)
        if (!meshResult.ok) throw "Procgen rock mesh generation failed"
        textureResult <- procgen.generateTexture("tex.stone", rockParams, gfx)
        if (!textureResult.ok) throw "Procgen stone texture generation failed"
        normalResult <- procgen.generateNormalImage("tex.stone", rockParams)
        if (!normalResult.ok) throw "Procgen normal image generation failed"
        normalTexture <- gfx.newTexture(normalResult.value, true, true)
        if (normalTexture == null) throw "Procgen normal texture upload failed"
        camera <- eve.Camera3D()
        camera.setEye(3.25, 2.15, 5.1)
        camera.setTarget(-0.4, -0.1, 0.0)
        camera.setFov(37.0)
    )",
                      "lazy-procgen-rock.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveVehicleFrameSurface") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        vehicle <- eve.Vehicle()
        vehicle.registerVehiclesFromJson(
            "[{\"id\":\"probe\",\"mobility\":\"kinematic\",\"maxSpeed\":80,\"accel\":60}]"
        )
        probeVehicle <- vehicle.newVehicle("probe", 0.0, 0.0, 0.0, "probe")
        if (!(probeVehicle instanceof eve.VehicleEntity)) throw "Vehicle entity binding was replaced"
        vehicle.setPosition(probeVehicle, 1.0, 2.0)
        if (vehicle.getX(probeVehicle) != 1.0 || vehicle.getY(probeVehicle) != 2.0)
            throw "Vehicle state binding was lost"
        vehicle.moveTo(probeVehicle, 10.0, 0.0)
        vehicle.update(0.1)
        local visible = false
        foreach (candidate in eve.view(eve.VehicleEntity)) {
            if (candidate.getId() == probeVehicle.getId()) visible = true
        }
        if (!visible) throw "Vehicle ECS view binding was lost"
    )",
                      "lazy-vehicle-frame.nut");
}

// module_manager_registry.cpp
namespace {
class RegistryProbe final : public eve::Module {
public:
    inline static int live = 0;
    RegistryProbe() { ++live; }
    ~RegistryProbe() override { --live; }
    std::string         getName() const override { return "RegistryStorageProbe"; }
    static eve::Module* create() {
        auto* instance = new RegistryProbe;
        eve::ModuleManager::insert("RegistryStorageProbe", instance);
        return instance;
    }
};
}  // namespace

TEST_CASE("ModuleManager.RegistryRollbackOwnsCreatedInstance") {
    using eve::ModuleManager;
    auto started = ModuleManager::beginPluginRegistration();
    REQUIRE(started.ok());
    ModuleManager::register_module("RegistryStorageProbe", &RegistryProbe::create, nullptr);
    auto* instance = ModuleManager::requireInstance<RegistryProbe>("RegistryStorageProbe");
    REQUIRE(instance != nullptr);
    CHECK_EQ(ModuleManager::find("RegistryStorageProbe"), instance);
    CHECK_EQ(RegistryProbe::live, 1);
    auto nested = ModuleManager::beginPluginRegistration();
    CHECK(!nested.ok());
    auto rolledBack = ModuleManager::finishPluginRegistration(false);
    REQUIRE(rolledBack.ok());
    CHECK_EQ(RegistryProbe::live, 0);
    CHECK(ModuleManager::find("RegistryStorageProbe") == nullptr);
    auto inactive = ModuleManager::finishPluginRegistration(true);
    CHECK(!inactive.ok());
}

TEST_CASE("ModuleManager.RegistryConflictPreservesCommittedInstance") {
    using eve::ModuleManager;
    auto started = ModuleManager::beginPluginRegistration();
    REQUIRE(started.ok());
    ModuleManager::register_module("RegistryStorageProbe", &RegistryProbe::create, nullptr);
    auto* instance  = ModuleManager::requireInstance<RegistryProbe>("RegistryStorageProbe");
    auto  committed = ModuleManager::finishPluginRegistration(true);
    REQUIRE(committed.ok());
    auto again = ModuleManager::beginPluginRegistration();
    REQUIRE(again.ok());
    ModuleManager::register_module("RegistryStorageProbe", &RegistryProbe::create, nullptr);
    auto conflicted = ModuleManager::finishPluginRegistration(true);
    CHECK(!conflicted.ok());
    CHECK_EQ(ModuleManager::find("RegistryStorageProbe"), instance);
    CHECK_EQ(RegistryProbe::live, 1);
}
}  // namespace test_module_lazy_bind
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mouse)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* mouse_content;
// mouse.cpp
namespace test_mouse {
using namespace eve;


UnitSciptTest(Mouse, mouse_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(Mouse, "Mouse." #func) { REQUIRE(callScript(#func)); }

// clang-format off
TestScript(basic)
TestScript(relativeMode)
TestScript(visibility)
// clang-format on
}  // namespace test_mouse
#undef TestScript
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_npc_ai_script)
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "npc_ai/NpcAiModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// npc_ai_script.cpp
namespace test_npc_ai_script {
TEST_CASE("npcAiScript.worldOwnsSignalAndBlackboardAgents") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::npc_ai::NpcAi::expose(eve);
    vm.run(vm.compileSource(R"(
        npc <- eve.NpcAi();
        created <- npc.newWorld(256, 32);
        world <- created.value;
        behaviorJson <- "{\"id\":\"guard\",\"schemaVersion\":1,\"initialState\":\"idle\",\"blackboardSchema\":[{\"key\":\"enemy\",\"type\":\"String\",\"required\":false}],\"states\":[{\"id\":\"idle\",\"transitions\":[{\"targetState\":\"alert\",\"signal\":\"enemy_seen\",\"priority\":10}]},{\"id\":\"alert\",\"enterConditions\":[{\"key\":\"enemy\",\"op\":\"Exists\"}]}]}";
        registered <- world.registerBehavior(behaviorJson);
        validated <- world.validateBehavior(behaviorJson);
        agentResult <- world.createAgent("guard");
        agent <- agentResult.value;
        wrote <- world.setBlackboardString(agent, "enemy", "player");
        first <- world.tick(1, 0.016, 8, 8);
        signaled <- world.signal(agent, "enemy_seen");
        second <- world.tick(2, 0.016, 8, 8);
        snap <- world.snapshot(agent);
        remembered <- world.remember(agent, "player", "sight", 0.9, 2, 30, "{}");
        destroyed <- world.destroyAgent(agent);
        stale <- world.isAgentStale(agent);
        afterDestroy <- world.snapshot(agent);
        activeState <- snap.value.activeState;
        ownership <- world.ownership();
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK_EQ(vm.find("ownership").toString(), std::string("owned"));
    CHECK(vm.find("registered").toTable().get<bool>("ok"));
    CHECK(vm.find("validated").toTable().get<bool>("ok"));
    CHECK(vm.find("agentResult").toTable().get<bool>("ok"));
    CHECK(vm.find("wrote").toTable().get<bool>("ok"));
    CHECK(vm.find("first").toTable().get<bool>("ok"));
    CHECK(vm.find("signaled").toTable().get<bool>("ok"));
    CHECK(vm.find("second").toTable().get<bool>("ok"));
    CHECK(vm.find("snap").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("activeState").toString(), std::string("alert"));
    CHECK(vm.find("remembered").toTable().get<bool>("ok"));
    CHECK(vm.find("destroyed").toTable().get<bool>("ok"));
    CHECK(vm.find("stale").toBool());
    CHECK(!vm.find("afterDestroy").toTable().get<bool>("ok"));
}

TEST_CASE("npcAiScript.rejectsOutOfRangeFieldsAndHonorsZeroMemory") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::npc_ai::NpcAi::expose(eve);
    vm.run(vm.compileSource(R"(
        npc <- eve.NpcAi();
        world <- npc.newWorld(64, 0).value;
        wrapVersion <- "{\"id\":\"guard\",\"schemaVersion\":4294967297,\"initialState\":\"idle\",\"states\":[{\"id\":\"idle\"}]}";
        negativePriority <- "{\"id\":\"guard\",\"schemaVersion\":1,\"initialState\":\"idle\",\"states\":[{\"id\":\"idle\",\"transitions\":[{\"targetState\":\"idle\",\"priority\":-1}]}]}";
        validBehavior <- "{\"id\":\"guard\",\"schemaVersion\":1,\"initialState\":\"idle\",\"states\":[{\"id\":\"idle\"}]}";
        badVersion <- world.validateBehavior(wrapVersion);
        badPriority <- world.validateBehavior(negativePriority);
        registered <- world.registerBehavior(validBehavior);
        agent <- world.createAgent("guard").value;
        oversizedBudget <- world.tick(1, 0.016, 4294967296, 8);
        remembered <- world.remember(agent, "player", "sight", 0.5, 1, 10, "{}");
        okTick <- world.tick(1, 0.016, 8, 8);
    )"));
    CHECK(!vm.find("badVersion").toTable().get<bool>("ok"));
    CHECK(!vm.find("badPriority").toTable().get<bool>("ok"));
    CHECK(vm.find("registered").toTable().get<bool>("ok"));
    CHECK(!vm.find("oversizedBudget").toTable().get<bool>("ok"));
    CHECK(!vm.find("remembered").toTable().get<bool>("ok"));
    CHECK(vm.find("okTick").toTable().get<bool>("ok"));
}
}  // namespace test_npc_ai_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_orders)
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "orders/CommandQueue.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// orders.cpp
namespace test_orders {
using namespace eve::orders;

namespace {
[[maybe_unused]] std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}
}  // namespace


TEST_CASE("orders.script.queueAndPayloadApi") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queueResult = module.newQueueOwned();
        local queue = queueResult.ok ? queueResult.value : null;
        local idResult = queue == null ? { ok=false } : queue.append("custom_action", 7, 1.0);
        local id = idResult.ok ? idResult.value : "";
        local order = queue.current();
        local payloadResult = order.getPayload().setJson("context", "{\"zone\":3}");
        if (queueResult.ok && idResult.ok && payloadResult.ok && order.getId() == id && order.getKind() == "custom_action" &&
            order.getPayload().getJson("context") == "{\"zone\":3}") {
            local completed = queue.complete(id);
            if (completed.ok) result = order.getState();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("completed"));
}

TEST_CASE("orders.script.ownedQueueReplace") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queueResult = module.newQueueOwned();
        if (!queueResult.ok) throw queueResult.status.summary;
        local queue = queueResult.value;
        local first = queue.append("move", 1, 0.0);
        if (!first.ok) throw first.status.summary;
        local replaced = queue.replace("move", 10, 0.0);
        if (!replaced.ok) throw replaced.status.summary;
        local order = queue.current();
        if (order != null && order.getId() == replaced.value && order.getKind() == "move" &&
            order.getState() == "active") {
            local payloadResult = order.getPayload().setJson("x", "400");
            if (payloadResult.ok && order.getPayload().has("x"))
                result = "replaced";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("replaced"));
}
}  // namespace test_orders
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_biome_controller)
#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "PrivateTestScope.h"
#include "animation/AnimSkin.h"
#include "common/SquirrelBinding.h"
#include "graphics/Graphics.h"
#include "procgen/PcgFrameRateManager.h"
#include "procgen/PcgSpawnProgress.h"
#include "procgen/PcgTaskQueue.h"
#include "procgen/Procgen.h"
#include "procgen/animation/PcgSkinnedMesh.h"
#include "procgen/animation/ProcgenAnimation.h"
#include "procgen/heightmap/PcgBiomeController.h"
#include "procgen/heightmap/PcgRuntimeStamper.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// pcg_biome_controller.cpp
namespace test_pcg_biome_controller {
// pcg_biome_controller.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgBiomeController.bindsRealVm") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
      local b=eve.PcgBiomeController();
      assert(b.configure(10.0,20.0,30.0,50.0,200.0,3).ok);
      assert(b.getLoadMode()==3 && b.getRange()==50.0);
      assert(b.getRegularCenterX()==10.0 && b.getRegularCenterY()==20.0 && b.getRegularCenterZ()==30.0);
      assert(b.getRegularSize()==99.5 && b.getImpostorSize()==299.5);
      assert(b.tierAt(10.0,20.0,30.0)==0 && b.tierAt(110.0,20.0,30.0)==1);
      assert(b.fitToTerrain(-100.0,7.0,-50.0,200.0,80.0,100.0).ok);
      assert(b.getRegularCenterX()==0.0 && b.getRegularCenterY()==7.0 && b.getRange()==100.0);
    )"));
}

// pcg_frame_rate_manager.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgFrameRateManager.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    exposePcgFrameRateManagerBindings(table);
    vm.run(vm.compileSource(R"(
  local m=eve.PcgFrameRateManager();assert(m.configure(60,0.5,0,5,2).ok);
  for(local i=0;i<6;i++)assert(m.update(0.1,1.0).ok);
  assert(m.getQuality()==1&&m.getQualityChanged());local p=m.getPreset();
  assert(p.treeDistance==500.0&&p.detailObjectDensity==0.25&&p.heightmapMaximumLod==1);
 )"));
}

// pcg_runtime_stamper.cpp
using namespace eve::procgen;


// pcg_skinned_mesh.cpp
using namespace eve;

namespace {
[[maybe_unused]] procgen::MeshBuild triangle(const char* material, float offset) {
    procgen::MeshBuild mesh;
    mesh.setActiveGroup(material);
    mesh.addVertex(offset, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F);
    mesh.addVertex(offset + 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F);
    mesh.addVertex(offset, 1.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

[[maybe_unused]] std::array<float, 16> identityBind() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

[[maybe_unused]] std::unique_ptr<animation::AnimSkin> skinFor(const procgen::MeshBuild& mesh, std::vector<int> bones,
                                                              std::vector<std::array<float, 16>> binds,
                                                              std::vector<int> joints, std::vector<float> weights) {
    animation::AnimSkinStreamData data;
    data.vertexCount         = mesh.getVertexCount();
    data.bindPositions       = mesh.positions();
    data.bindNormals         = mesh.normals();
    data.skeletonBones       = std::move(bones);
    data.inverseBindMatrices = std::move(binds);
    for (int bone : data.skeletonBones) data.boneNames.push_back("bone-" + std::to_string(bone));
    data.vertexSkinJoints = std::move(joints);
    data.vertexWeights    = std::move(weights);
    auto result           = animation::AnimSkin::fromStreams(std::move(data));
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
}  // namespace


TEST_CASE("procgen.animation.pcgSkinnedMesh.realVmCombinesOwnedResult") {
    auto mesh = triangle("skin", 0.F);
    auto bind = identityBind();
    auto skin =
        skinFor(mesh, {7}, {bind}, {0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    procgen::Procgen                    procgen;
    procgen_animation::ProcgenAnimation bridge;
    ssq::VM                             vm(1024);
    auto                                table     = vm.addTable("eve");
    auto                                skinClass = table.addClass<animation::AnimSkin>(
        "AnimSkin", std::function<animation::AnimSkin*()>([]() -> animation::AnimSkin* { return nullptr; }), true);
    skinClass.addFunc("getVertexCount", &animation::AnimSkin::getVertexCount);
    procgen.expose(table);
    bridge.expose(table);
    vm.addFunc("pcgSkinMesh", [&]() { return &mesh; });
    vm.addFunc("pcgSkin", [&]() { return skin.get(); });
    vm.run(vm.compileSource(R"(
        local transform=eve.PcgMeshTransform();
        local plan=eve.PcgSkinnedMeshPlan();
        assert(plan.appendSource(pcgSkinMesh(),pcgSkin(),transform,"skin").ok);
        local output=eve.PcgSkinnedMeshResult();
        local module=eve.ProcgenAnimation();
        assert(module.combinePcgSkinnedMeshes(output,plan).ok);
        assert(output.isValid() && output.getSkin().getVertexCount()==3);
        local combined=output.copyMesh();
        assert(combined.getVertexCount()==3 && combined.getIndexCount()==3);
    )"));
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.realGraphicsAcceptsCombinedSkinStreams") {
    auto mesh = triangle("skin", 0.F);
    auto bind = identityBind();
    auto skin =
        skinFor(mesh, {3}, {bind}, {0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    procgen_animation::PcgSkinnedMeshPlan plan;
    procgen::PcgMeshTransform             transform;
    REQUIRE(plan.appendSource(mesh, *skin, transform, "skin").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output, plan).ok());
    GfxFixture       fixture(96, 96, true);
    procgen::Procgen procgen;
    auto             gpuMesh = procgen.uploadMeshBorrowed(*output.mesh(), *fixture.gfx);
    REQUIRE(gpuMesh.isBound());
    REQUIRE(output.skin()->bindGpuMesh(fixture.gfx, gpuMesh.get()));
}

// pcg_spawn_progress.cpp
using namespace eve::procgen;


// pcg_task_queue.cpp
using namespace eve::procgen;
}  // namespace test_pcg_biome_controller
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_color_preview_sync)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "ui/PcgColorPreviewSync.h"
#include "ui/PcgControllerSelection.h"
#include "ui/PcgDraggableWindow.h"
#include "ui/PcgLoadingScreen.h"
#include "ui/PcgScreenshotSavedNotice.h"
#include "ui/PcgTooltip.h"
#include "ui/UI.h"
#include "zeroerr/unittest.h"
// pcg_color_preview_sync.cpp
namespace test_pcg_color_preview_sync {
// pcg_color_preview_sync.cpp
using eve::ui::PcgColorPreviewSync;


// pcg_controller_selection.cpp
using eve::ui::PcgControllerSelection;


// pcg_draggable_window.cpp
using eve::ui::PcgDraggableWindow;
using eve::ui::PcgUiRequestStatus;


// pcg_loading_screen.cpp
using eve::ui::PcgLoadingScreen;


TEST_CASE("ui.pcgLoadingScreen.invalidEventsPreservePublishedStateAndBindRealVm") {
    PcgLoadingScreen screen;
    REQUIRE(screen.configure(1.f).ok());
    screen.onLoadProgressStarted();
    REQUIRE(screen.onLoadProgressUpdated(.4f).ok());
    CHECK(!screen.onLoadProgressUpdated(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(screen.getProgress(), .4f);
    CHECK(!screen.tick(-1.f).ok());
    CHECK_EQ(screen.getBackgroundAlpha(), 1.f);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::ui::UI::expose(table);
    vm.run(vm.compileSource(R"(
      local s=eve.PcgLoadingScreen();assert(s.configure(4.0).ok);
      s.onLoadProgressStarted();assert(s.getCanvasVisible() && s.getProgress()==0.0);
      assert(s.onLoadProgressUpdated(1.0).ok && s.getFading());
      assert(s.tick(0.25).ok && !s.getCanvasVisible() && s.getBackgroundAlpha()==0.0);
      s.beginTimeout();local i=s.addMissingScene("West","West Impostor");assert(i.ok);
      assert(s.addRegularReference(i.value,"Biome").ok && s.endTimeout().ok);
      assert(s.getTimeoutMessage().len()>0);
    )"));
}

// pcg_screenshot_saved_notice.cpp
using eve::ui::PcgScreenshotSavedNotice;


// pcg_tooltip.cpp
using eve::ui::PcgTooltipManager;
}  // namespace test_pcg_color_preview_sync
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_terrain_watcher)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
// pcg_terrain_watcher.cpp
namespace test_pcg_terrain_watcher {
// pcg_terrain_watcher.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgTerrainWatcher.bindsRealVm") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
        local w=eve.PcgTerrainWatcher();
        w.beginScan();assert(w.addTerrain("a").ok);assert(w.commitScan().value==0);
        w.beginScan();assert(w.addTerrain("b").ok);assert(w.commitScan().value==2);
        assert(w.getTerrainCount()==1 && w.getChangeCount()==2);
        assert(w.getChangeTerrainId(0).value=="b" && w.getChangeType(0).value==0);
        assert(w.getChangeTerrainId(1).value=="a" && w.getChangeType(1).value==1);
    )"));
}

// pcg_tree_manager.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgTreeManager.bindsRealVm") {
    PointSet trees;
    trees.add(2, 0, 2);
    trees.add(8, 0, 8);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("trees", [&]() { return &trees; });
    vm.run(vm.compileSource(R"(
      local m=eve.PcgTreeManager();assert(m.reset(0.0,0.0,10.0,10.0).ok);
      assert(m.addTrees(trees(),4).value==2);assert(m.addTree(5.0,5.0,1).value==3);
      assert(m.getCount()==3 && m.countInRange(5.0,5.0,3.0).value==3);
    )"));
}
}  // namespace test_pcg_terrain_watcher
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_cloth)
#include <array>
#include <cmath>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModel.h"
extern const char* physics_cloth_content;
// physics_cloth.cpp
namespace test_physics_cloth {
using namespace eve;


UnitSciptTest(PhysicsClothScriptTest, physics_cloth_content);

TEST_CASE_FIXTURE(PhysicsClothScriptTest, "PhysicsCloth.scriptBindings") { REQUIRE(callScript("basic")); }
}  // namespace test_physics_cloth
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_policyregistry)
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "policyregistry/PolicyRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// policyregistry.cpp
namespace test_policyregistry {
using namespace eve::policyregistry;


TEST_CASE("policyregistry.scriptDiscoversButDoesNotStoreCallbacks") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.PolicyRegistryModule();
        local registry = module.newRegistry();
        local callback = function(subject) { return subject + ":handled"; };
        local inserted = registry.insert("administration", "governor", 1, 8, true, "script",
                                         "governor.policy", "{\"owner\":\"game-script\"}");
        if (inserted.ok) {
            local generation = inserted.value.generation;
            local resolved = registry.resolve("administration", "governor");
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            local restored = copy.restoreJson(snapshot);
            local resolvedCopy = copy.resolveHandle("administration", "governor", generation);
            if (resolved.ok && restored.ok && copy.select("administration").getName() == "governor" &&
                resolvedCopy.ok && resolvedCopy.value.kind == "script")
                result = callback("general-7");
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("general-7:handled"));
}
}  // namespace test_policyregistry
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_module_assembly)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "procgen/ModuleAssembly.h"
#include "zeroerr/unittest.h"
// procgen_module_assembly.cpp
namespace test_procgen_module_assembly {
using namespace eve::procgen;


TEST_CASE("procgen.moduleAssembly.squirrelBuildsValidatedPlacementTransforms") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local plan = eve.ProcgenModuleAssemblyPlan();
        assert(plan.configure(4.0, 4.0, -4, 4, -4, 4, 3, true, true).ok);
        assert(plan.registerModule("wall", 1, 1).ok);
        assert(plan.addConnector("wall", 0, 0, 1, "wall", "wall").ok);
        assert(plan.addConnector("wall", 0, 0, 3, "wall", "wall").ok);
        assert(plan.place("wall", 0, 0, 0, 0).ok);
        assert(plan.place("wall", 1, 0, 0, 0).ok);
        local rejected = plan.place("wall", 3, 3, 0, 0);
        assert(!rejected.ok && rejected.status.code == "rejected");
        assert(plan.getPlacementCount() == 2);
        assert(plan.getPlacementModule(1) == "wall");
        assert(plan.getPlacementX(1) == 4.0);
        assert(plan.getPlacementY(1) == 0.0);
        assert(plan.getPlacementZ(1) == 0.0);
        assert(plan.getPlacementYawDegrees(1) == 0.0);

        local volume = eve.ProcgenModuleAssemblyPlan();
        assert(volume.configure(4.0, 4.0, 0, 2, 0, 2, 4, true, true).ok);
        assert(volume.setAllowedQuarterTurns(true, false, true, false).ok);
        assert(volume.setMinimumSupportRatio(1.0).ok);
        assert(volume.registerVolumeModule("base", 1, 1, 1).ok);
        assert(volume.registerVolumeModule("upper", 1, 1, 2).ok);
        assert(volume.addVolumeConnector("base", 0, 0, 0, 4, "support", "load").ok);
        assert(volume.addVolumeConnector("upper", 0, 0, 0, 5, "load", "support").ok);
        assert(volume.place("base", 0, 0, 0, 0).ok);
        assert(volume.place("upper", 0, 0, 1, 0).ok);
    )"));
}
}  // namespace test_procgen_module_assembly
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_runtime_generation)
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <utility>
#include "PrivateTestScope.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_runtime_generation.cpp
namespace test_procgen_runtime_generation {
using namespace eve::procgen;

namespace {

/** @brief Owns a module-backed runtime-generation handle for one test. */
class RuntimeLease {
public:
    RuntimeLease() = default;
    RuntimeLease(Procgen& owner, ProcgenRuntimeGenerationHandleRef handle) : owner_(&owner), handle_(handle) {}

    RuntimeLease(const RuntimeLease&)            = delete;
    RuntimeLease& operator=(const RuntimeLease&) = delete;
    RuntimeLease(RuntimeLease&& other) noexcept : owner_(other.owner_), handle_(other.handle_) {
        other.owner_  = nullptr;
        other.handle_ = {};
    }
    RuntimeLease& operator=(RuntimeLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = nullptr;
        other.handle_ = {};
        return *this;
    }
    ~RuntimeLease() { reset(); }

    /** @brief Releases the scheduler handle and explicitly observes the Result. */
    void reset() noexcept {
        if (!owner_ || !handle_.isValid()) return;
        auto result = owner_->release(handle_);
        result.ignore("test runtime-generation cleanup");
        owner_  = nullptr;
        handle_ = {};
    }

    /** @brief Provides a short-lived borrowed view of the scheduler. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> view() const noexcept {
        return owner_ ? owner_->resolveRuntimeGeneration(handle_) : eve::script::Borrowed<RuntimeGeneration>();
    }

    /** @brief Preserves the concise `runtime->operation()` test syntax. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> operator->() const noexcept { return view(); }

private:
    Procgen*                          owner_ = nullptr;
    ProcgenRuntimeGenerationHandleRef handle_{};
};

/** @brief Converts a checked scheduler allocation Result into a test lease. */
[[maybe_unused]] RuntimeLease requireRuntime(Procgen& proc, uint32_t worldSeed) {
    auto       result = proc.newRuntimeGenerationHandle(worldSeed);
    const bool ok     = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
    }
    REQUIRE(ok);
    return RuntimeLease(proc, std::move(result).takeValue());
}

/** @brief Gives an owning lifetime to a request allocated by the scheduler API. */
using RequestLease = std::unique_ptr<ProcgenCellRequest>;

[[nodiscard]] RequestLease ownRequest(ProcgenCellRequest* request) noexcept { return RequestLease(request); }

}  // namespace


TEST_CASE("procgen.runtimeGeneration.squirrelCompletesCleanupTicketsAtomically") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(81);
        local pointsResult = procgen.sampleGrid(1, 1, 1.0, 18, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            local points = pointsResult.value;
            runtime.addLevel(10.0, 16.0, 1.5);
            runtime.setMaxGenerating(8);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local first = runtime.nextGenerate();
            local second = runtime.nextGenerate();
            if (first != null && second != null && runtime.completeGeneration(first, points) &&
                runtime.completeGeneration(second, points)) {
                runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
                local firstCleanup = runtime.nextCleanup();
                local secondCleanup = runtime.nextCleanup();
                local completed = runtime.completeCleanupsAtomic([firstCleanup, secondCleanup]);
                if (completed.ok && completed.value == "2" &&
                    !runtime.isRequestCurrent(firstCleanup) && !runtime.isRequestCurrent(secondCleanup))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


TEST_CASE("procgen.runtimeGeneration.squirrelAppliesAndInspectsCellDelta") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(934);
        local pointsResult = procgen.sampleGrid(2, 1, 1.0, 17, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            local points = pointsResult.value;
            runtime.addLevel(10.0, 6.0, 1.5);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local request = runtime.nextGenerate();
            if (request != null && runtime.completeGeneration(request, points)) {
                points.setPosition(0, 9.0, 2.0, -3.0);
                local update = runtime.applyCellUpdate(
                    request.getLevel(), request.getX(), request.getZ(), "1", points);
                local delta = runtime.getCellDelta(request.getLevel(), request.getX(), request.getZ());
                if (update.ok && update.value == "2" && delta != null &&
                    delta.getAddedCount() == 0 && delta.getUpdatedCount() == 1 &&
                    delta.getRemovedCount() == 0 && delta.getTargetCount() == 2 &&
                    delta.getBaseFingerprint() != delta.getTargetFingerprint() &&
                    delta.getUpdated().getPointId(0) == points.getPointId(0))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


TEST_CASE("procgen.runtimeGeneration.squirrelAdvancesBudgetedRefresh") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local created = procgen.newRuntimeGeneration(1704);
        if (created.ok) {
            local runtime = created.value;
            runtime.addLevel(10.0, 25.0, 1.5);
            runtime.setRefreshWorkBudget(2);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local total = 0;
            while (runtime.isRefreshPending()) {
                local advanced = runtime.continueGenerationRefresh();
                if (!advanced.ok) break;
                total += advanced.value.tointeger();
            }
            if (runtime.getRefreshWorkBudget() == 2 &&
                runtime.getCommittedRefreshRevision() == 1 &&
                runtime.getPendingGenerateCount() > 0 && total > 0)
                result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_procgen_runtime_generation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_scene_sink)
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_scene_sink.cpp
namespace test_procgen_scene_sink {
// procgen_scene_sink.cpp
using namespace eve::procgen;

namespace {

class MockProcgenSceneSink final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId, const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        const auto                            previous = batches.find(batchId);
        std::unordered_map<std::string, bool> oldIds;
        if (previous != batches.end())
            for (const auto& instance : previous->second) oldIds[instance.id] = true;
        Stats stats;
        for (const auto& instance : instances) {
            if (oldIds.erase(instance.id) != 0)
                ++stats.reused;
            else
                ++stats.created;
        }
        stats.removed      = int(oldIds.size());
        batches[batchId]   = instances;
        revisions[batchId] = revisions[batchId] + 1;
        latest[batchId]    = stats;
        return true;
    }
    eve::Result<uint64_t> replaceBatch(const std::string& batchId, uint64_t targetRevision,
                                       const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        if (targetRevision == 0 || batchRevision(batchId) >= targetRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock snapshot revision is stale"));
        if (!applyBatch(batchId, instances))
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "mock snapshot commit failed"));
        ++replaceCalls;
        revisions[batchId] = targetRevision;
        return eve::Result<uint64_t>::success(targetRevision);
    }
    eve::Result<uint64_t> replaceBatches(const std::vector<eve::ProcgenBatchSnapshot>& snapshots) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& snapshot : snapshots) {
            if (snapshot.batchId.empty() || snapshot.targetRevision == 0 ||
                nextRevisions[snapshot.batchId] >= snapshot.targetRevision)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock batch transaction is stale"));
            nextBatches[snapshot.batchId]   = snapshot.instances;
            nextRevisions[snapshot.batchId] = snapshot.targetRevision;
        }
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(snapshots.size()));
    }
    eve::Result<uint64_t> applyDelta(const std::string& batchId, const eve::ProcgenInstanceDelta& delta) override {
        const auto current = revisions.find(batchId);
        if (current == revisions.end() || current->second != delta.baseRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock delta revision is stale"));
        lastDeltaBatch = batchId;
        lastDelta      = delta;
        ++deltaCalls;
        revisions[batchId] = delta.targetRevision;
        return eve::Result<uint64_t>::success(delta.targetRevision);
    }
    uint64_t batchRevision(const std::string& batchId) const override {
        const auto found = revisions.find(batchId);
        return found == revisions.end() ? 0 : found->second;
    }
    bool removeBatch(const std::string& batchId) override {
        const auto found = batches.find(batchId);
        if (found == batches.end()) return false;
        latest[batchId] = {0, 0, int(found->second.size())};
        batches.erase(found);
        revisions.erase(batchId);
        return true;
    }
    eve::Result<uint64_t> removeBatches(const std::vector<std::string>& batchIds) override {
        return removeBatchesCoordinated(batchIds, [] { return eve::Result<void>::success(); });
    }
    eve::Result<uint64_t> removeBatchesCoordinated(
        const std::vector<std::string>&           batchIds,
        const std::function<eve::Result<void>()>& commitPreparedOwner) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& batchId : batchIds) {
            if (nextBatches.erase(batchId) != 1)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "mock batch is unpublished"));
            nextRevisions.erase(batchId);
        }
        if (!commitPreparedOwner)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "mock commit is missing"));
        auto ownerCommit = commitPreparedOwner();
        if (!ownerCommit.ok()) return eve::Result<uint64_t>::failure(ownerCommit.status());
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(batchIds.size()));
    }
    int instanceCount(const std::string& batchId) const override {
        const auto found = batches.find(batchId);
        return found == batches.end() ? 0 : int(found->second.size());
    }
    int lastCreatedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.created;
    }
    int lastReusedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.reused;
    }
    int lastRemovedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.removed;
    }

    struct Stats {
        int created = 0;
        int reused  = 0;
        int removed = 0;
    };
    std::unordered_map<std::string, std::vector<eve::ProcgenInstanceDesc>> batches;
    std::unordered_map<std::string, uint64_t>                              revisions;
    std::unordered_map<std::string, Stats>                                 latest;
    std::string                                                            lastDeltaBatch;
    eve::ProcgenInstanceDelta                                              lastDelta;
    int                                                                    replaceCalls         = 0;
    int                                                                    deltaCalls           = 0;
    bool                                                                   failBatchTransaction = false;
};

}  // namespace


TEST_CASE("procgen.sceneSink.squirrelCompletesCoordinatedCleanup") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(33);
        local pointsResult = procgen.sampleGrid(1, 1, 1.0, 34, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            runtime.addLevel(10.0, 6.0, 1.5);
            runtime.updateSource(1.0, 1.0, 1.0, 0.0);
            local generated = runtime.nextGenerate();
            if (generated != null && runtime.completeGeneration(generated, pointsResult.value)) {
                local published = procgen.synchronizeCellInstances(
                    "script-cleanup", runtime, generated, "asset", "stone");
                runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
                local cleanup = runtime.nextCleanup();
                local completed = procgen.completeCellCleanupAtomic(
                    "script-cleanup", [runtime], [cleanup]);
                if (published.ok && completed.ok && completed.value == "1" &&
                    !runtime.isRequestCurrent(cleanup))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
    CHECK(sink.batches.empty());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}


// procgen_schema_script.cpp
TEST_CASE("procgen.script.reflects_schema_and_generates_from_dynamic_values") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        algorithm <- "cave.cellular";
        paramsResult <- procgen.newParams();
        paramsResultOk <- paramsResult.ok;
        params <- paramsResult.ok ? paramsResult.value : null;
        defaultsResult <- params == null ? { ok = false } :
            procgen.applyAlgorithmDefaults(algorithm, params);
        defaultsApplied <- defaultsResult.ok;
        paramCount <- procgen.getAlgorithmParamCount(algorithm);
        fieldsComplete <- true;
        fillIndex <- -1;
        for (local i = 0; i < paramCount; ++i) {
            if (procgen.getAlgorithmParamKey(algorithm, i) == "" ||
                procgen.getAlgorithmParamLabel(algorithm, i) == "" ||
                procgen.getAlgorithmParamKind(algorithm, i) == "") fieldsComplete = false;
            if (procgen.getAlgorithmParamKey(algorithm, i) == "fill") fillIndex = i;
        }
        widthSet <- params == null ? { ok = false } : params.setInt("width", 40);
        heightSet <- params == null ? { ok = false } : params.setInt("height", 24);
        seedSet <- params == null ? { ok = false } : params.setInt("seed", 77);
        fillSet <- params == null ? { ok = false } : params.setFloat("fill", 0.38);
        widthSetOk <- widthSet.ok;
        heightSetOk <- heightSet.ok;
        seedSetOk <- seedSet.ok;
        fillSetOk <- fillSet.ok;
        gridResult <- params == null ? { ok = false, hasValue = false } :
            procgen.generate(algorithm, params);
        gridOk <- gridResult.ok && gridResult.hasValue &&
            gridResult.value.ownership() == "owned" && !gridResult.value.isStale();
        width <- params == null ? 0 : params.getWidth();
        height <- params == null ? 0 : params.getHeight();
        fillKind <- fillIndex < 0 ? "" : procgen.getAlgorithmParamKind(algorithm, fillIndex);
        fillMin <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMinimum(algorithm, fillIndex);
        fillMax <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMaximum(algorithm, fillIndex);
        wfcChoiceCount <- procgen.getAlgorithmParamChoiceCount("wfc.simple", 3);
        wfcFirstChoice <- procgen.getAlgorithmParamChoice("wfc.simple", 3, 0);
        algorithmSchemaResult <- procgen.getAlgorithmSchema(algorithm);
        algorithmSchemaOk <- algorithmSchemaResult.ok && algorithmSchemaResult.hasValue;
        algorithmSchema <- algorithmSchemaOk ? algorithmSchemaResult.value : null;
        genericAlgorithmId <- algorithmSchema == null ? "" : algorithmSchema.getId();
        textureParamsResult <- procgen.newParams();
        textureParamsResultOk <- textureParamsResult.ok;
        textureParams <- textureParamsResult.ok ? textureParamsResult.value : null;
        textureDefaultsResult <- textureParams == null ? { ok = false } :
            procgen.applyTextureRecipeDefaults("tex.rock", textureParams);
        textureDefaultsApplied <- textureDefaultsResult.ok;
        textureSchemaResult <- procgen.getTextureRecipeSchema("tex.rock");
        textureSchemaOk <- textureSchemaResult.ok && textureSchemaResult.hasValue;
        textureSchema <- textureSchemaOk ? textureSchemaResult.value : null;
        textureCategory <- textureSchema == null ? "" : textureSchema.getCategory();
        textureScaleFound <- false;
        for (local i = 0; textureSchema != null && i < textureSchema.getParamCount(); ++i)
            if (textureSchema.getParamKey(i) == "scale") textureScaleFound = true;
        pbrParamsResult <- procgen.newParams();
        pbrParamsResultOk <- pbrParamsResult.ok;
        pbrParams <- pbrParamsResult.ok ? pbrParamsResult.value : null;
        sizeSet <- pbrParams == null ? { ok = false } : pbrParams.setSize(16, 12);
        pbrDefaultsResult <- pbrParams == null ? { ok = false } :
            procgen.applyPbrRecipeDefaults("pbr.rock", pbrParams);
        pbrDefaultsApplied <- pbrDefaultsResult.ok && sizeSet.ok;
        pbrSchemaResult <- procgen.getPbrRecipeSchema("pbr.rock");
        pbrSchemaOk <- pbrSchemaResult.ok && pbrSchemaResult.hasValue;
        pbrSchema <- pbrSchemaOk ? pbrSchemaResult.value : null;
        pbrMetallicFound <- false;
        pbrNormalFound <- false;
        for (local i = 0; pbrSchema != null && i < pbrSchema.getParamCount(); ++i) {
            local key = pbrSchema.getParamKey(i);
            if (key == "metallic") pbrMetallicFound = true;
            if (key == "normalStrength") pbrNormalFound = true;
        }
        pbrSetResult <- pbrParams == null ? { ok = false, hasValue = false } :
            procgen.generatePbrMaterial("pbr.rock", pbrParams);
        pbrResultOk <- pbrSetResult.ok && pbrSetResult.hasValue &&
            pbrSetResult.value.ownership() == "owned" && !pbrSetResult.value.isStale();
        meshParamsResult <- procgen.newParams();
        meshParamsResultOk <- meshParamsResult.ok;
        meshParams <- meshParamsResult.ok ? meshParamsResult.value : null;
        meshDefaultsResult <- meshParams == null ? { ok = false } :
            procgen.applyMeshRecipeDefaults("mesh.fence", meshParams);
        meshDefaultsApplied <- meshDefaultsResult.ok;
        meshSchemaResult <- procgen.getMeshRecipeSchema("mesh.fence");
        meshSchemaOk <- meshSchemaResult.ok && meshSchemaResult.hasValue;
        meshSchema <- meshSchemaOk ? meshSchemaResult.value : null;
        meshSegmentsFound <- false;
        for (local i = 0; meshSchema != null && i < meshSchema.getParamCount(); ++i)
            if (meshSchema.getParamKey(i) == "segments") meshSegmentsFound = true;
        meshBuildResult <- meshParams == null ? { ok = false, hasValue = false } :
            procgen.buildMesh("mesh.fence", meshParams);
        meshBuildOk <- meshBuildResult.ok && meshBuildResult.hasValue &&
            meshBuildResult.value.ownership() == "owned" && !meshBuildResult.value.isStale();
    )"));

    CHECK(vm.find("paramsResultOk").toBool());
    CHECK(vm.find("defaultsApplied").toBool());
    CHECK(vm.find("widthSetOk").toBool());
    CHECK(vm.find("heightSetOk").toBool());
    CHECK(vm.find("seedSetOk").toBool());
    CHECK(vm.find("fillSetOk").toBool());
    CHECK(vm.find("gridOk").toBool());
    CHECK_GT(vm.find("paramCount").toInt(), 3);
    CHECK(vm.find("fieldsComplete").toBool());
    CHECK_EQ(vm.find("fillKind").toString(), std::string("float"));
    CHECK_EQ(vm.find("fillMin").toFloat(), 0.f);
    CHECK_EQ(vm.find("fillMax").toFloat(), 1.f);
    CHECK_EQ(vm.find("width").toInt(), 40);
    CHECK_EQ(vm.find("height").toInt(), 24);
    CHECK_EQ(vm.find("wfcChoiceCount").toInt(), 3);
    CHECK_EQ(vm.find("wfcFirstChoice").toString(), std::string("dungeon"));
    CHECK_EQ(vm.find("genericAlgorithmId").toString(), std::string("cave.cellular"));
    CHECK(vm.find("textureParamsResultOk").toBool());
    CHECK(vm.find("textureDefaultsApplied").toBool());
    CHECK_EQ(vm.find("textureCategory").toString(), std::string("Texture"));
    CHECK(vm.find("textureScaleFound").toBool());
    CHECK(vm.find("pbrParamsResultOk").toBool());
    CHECK(vm.find("pbrDefaultsApplied").toBool());
    CHECK(vm.find("pbrMetallicFound").toBool());
    CHECK(vm.find("pbrNormalFound").toBool());
    CHECK(vm.find("pbrResultOk").toBool());
    CHECK(vm.find("meshParamsResultOk").toBool());
    CHECK(vm.find("meshDefaultsApplied").toBool());
    CHECK(vm.find("meshSegmentsFound").toBool());
    CHECK(vm.find("meshBuildOk").toBool());
}

// procgen_shape_grammar.cpp
using namespace eve::procgen;


// procgen_spatial_data.cpp
using namespace eve::procgen;

namespace {

class FlatProcgenWorldQuery final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY, std::uint64_t) override {
        if (maxY < 4.f || minY > 4.f) return eve::Result<eve::ProcgenSurfaceHit>::success({});
        return eve::Result<eve::ProcgenSurfaceHit>::success({true, 91, x, 4.f, z, 0.f, 1.f, 0.f});
    }
};

struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

}  // namespace


TEST_CASE("procgen.spatialData.squirrelFacadeExposesComposablePrimitives") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local pointsResult = procgen.newPointSet();
        if (pointsResult.ok) {
            local points = pointsResult.value;
            points.add(1.0, 0.0, 1.0);
            points.add(5.0, 0.0, 5.0);
            points.add(9.0, 0.0, 9.0);
            points.setFloatAttribute(0, "priority", 2.0);
            points.setFloatAttribute(1, "priority", 0.5);
            points.setFloatAttribute(2, "priority", 3.0);
            points.setIntAttribute(0, "variant", 7);
            points.setBoolAttribute(0, "enabled", true);
            points.setVectorAttribute(0, "wind", 1.0, 2.0, 3.0);
            local worldResult = procgen.boxVolume(0.0, -1.0, 0.0, 10.0, 1.0, 10.0);
            local holeResult = procgen.sphereVolume(5.0, 0.0, 5.0, 1.5);
            if (worldResult.ok && holeResult.ok) {
                local domainResult = procgen.differenceSpatial(worldResult.value, holeResult.value);
                if (domainResult.ok) {
                    local filteredResult = procgen.filterSpatial(points, domainResult.value, false);
                    local priorityResult = procgen.filterFloatAttribute(points, "priority", 1.0, 4.0, false);
                    local transformedResult = procgen.transformPoints(
                        points, 2.0, 0.0, 3.0, 90.0, 1.0, 1.0, 1.0);
                    local transformed3DResult = procgen.transformPoints3D(
                        points, 0.0, 0.0, 0.0, 90.0, 0.0, 0.0, 1.0, 1.0, 1.0);
                    local unionResult = procgen.unionPoints(points, filteredResult.value);
                    local intersectResult = procgen.intersectPoints(points, filteredResult.value);
                    local pointDifferenceResult = procgen.differencePoints(points, filteredResult.value);
                    local copyResult = procgen.copyPoints(filteredResult.value, points, true);
                    local remapResult = procgen.remapDensity(points, 0.0, 1.0, 0.0, 0.5, true);
                    local mathResult = procgen.mathFloatAttribute(
                        points, "priority", "weightedPriority", "multiply", 2.0, 0.0);
                    local sampledResult = procgen.sampleSpatial(domainResult.value, 5.0, 7, 0.0);
                    local poissonResult = procgen.poissonDisk(10, 10, 2.0, 9, 32);
                    if (points.getAttributeType(0, "variant") == "int" &&
                        points.getAttributeType(0, "enabled") == "bool" &&
                        points.getAttributeType(0, "wind") == "vector" &&
                        points.getIntAttribute(0, "variant", 0) == 7 &&
                        points.getBoolAttribute(0, "enabled", false) &&
                        points.getVectorAttributeZ(0, "wind", 0.0) == 3.0 &&
                        filteredResult.ok && filteredResult.value.getCount() == 2 &&
                        priorityResult.ok && priorityResult.value.getCount() == 2 &&
                        transformedResult.ok && transformedResult.value.getCount() == 3 &&
                        transformed3DResult.ok && transformed3DResult.value.getY(0) == -1.0 &&
                        unionResult.ok && unionResult.value.getCount() == 3 &&
                        intersectResult.ok && intersectResult.value.getCount() == 2 &&
                        pointDifferenceResult.ok && pointDifferenceResult.value.getCount() == 1 &&
                        copyResult.ok && copyResult.value.getCount() == 6 &&
                        remapResult.ok && remapResult.value.getDensity(0) == 0.5 &&
                        mathResult.ok && mathResult.value.getFloatAttribute(
                            0, "weightedPriority", 0.0) == 4.0 &&
                        sampledResult.ok && sampledResult.value.getCount() > 0 &&
                        poissonResult.ok && poissonResult.value.getCount() > 0)
                        result = "ok";
                }
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


// procgen_spline_path.cpp
using namespace eve::procgen;

namespace {

[[maybe_unused]] SplinePoint point(float x, float y, float z) {
    SplinePoint value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

}  // namespace


// procgen_worker_contract.cpp
using namespace eve::procgen;

static_assert(std::is_copy_constructible_v<ProcgenGenerationJob>);
static_assert(!std::is_copy_constructible_v<RuntimeGeneration>);
static_assert(!std::is_move_constructible_v<RuntimeGeneration>);


TEST_CASE("procgen.worker.legacyIssueRejectsWrongThread") {
    RuntimeGeneration runtime(42);
    REQUIRE(runtime.addLevel(10.f, 4.f, 2.f) == 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto worker = std::async(std::launch::async, [&runtime] {
        std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
        auto                                checked = runtime.nextGenerationJob();
        return !request && !checked.ok();
    });
    CHECK(worker.get());
    CHECK(runtime.getGeneratingCount() == 0);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    CHECK(std::move(issued).takeValue().has_value());
}

TEST_CASE("procgen.runtimeGeneration.movesValueJobsAcrossWorkerBoundary") {
    RuntimeGeneration runtime(4201);
    REQUIRE(runtime.isOwnerThread());
    const int addedLevel = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(addedLevel, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    auto optionalJob = std::move(issued).takeValue();
    REQUIRE(optionalJob.has_value());
    ProcgenGenerationJob job   = *optionalJob;
    const int            level = job.getLevel();
    const int            x     = job.getX();
    const int            z     = job.getZ();

    auto worker    = std::async(std::launch::async, [job] {
        PointSet output;
        output.add(job.getMinX(), 0.f, job.getMinZ());
        return ProcgenGenerationCompletion(job, std::move(output));
    });
    auto committed = runtime.completeGenerationJob(worker.get());
    REQUIRE(committed.ok());
    const auto revision = std::move(committed).takeValue();
    CHECK_EQ(revision, uint64_t(1));
    CHECK(runtime.hasCell(level, x, z));
}

TEST_CASE("procgen.runtimeGeneration.rejectsWorkerThreadSchedulerMutation") {
    RuntimeGeneration runtime(4202);
    const int         addedLevel = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(addedLevel, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    auto optionalJob = std::move(issued).takeValue();
    REQUIRE(optionalJob.has_value());
    const ProcgenGenerationJob job = *optionalJob;

    auto workerAccepted = std::async(std::launch::async, [&runtime, job] {
        PointSet output;
        output.add(0.f, 0.f, 0.f);
        auto result = runtime.completeGenerationJob(ProcgenGenerationCompletion(job, std::move(output)));
        return result.ok();
    });
    CHECK(!workerAccepted.get());
    CHECK_EQ(runtime.getGeneratingCount(), 1);

    PointSet output;
    output.add(0.f, 0.f, 0.f);
    auto committed = runtime.completeGenerationJob(ProcgenGenerationCompletion(job, std::move(output)));
    REQUIRE(committed.ok());
}

TEST_CASE("procgen.script.newShapeGrammarIsTheDocumentedEntryPoint") {
    // docs/usr/modules/procgen/pointset-pipeline.md tells users to start from
    // `procgen.newShapeGrammar()`. The class and all of its methods were exposed but
    // nothing created an instance, so that documented sample could not run at all --
    // the binding check's reverse direction reported it as "documented but not bound".
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        created <- procgen.newShapeGrammar();
        grammar <- created.ok ? created.value : null;
        isProxy <- grammar != null && grammar instanceof eve.ProcgenShapeGrammar;
        added <- grammar == null ? false : grammar.addModule("Wall", "stone-wall-a", 4.0, 3.0);
        count <- grammar == null ? -1 : grammar.getModuleCount();
        symbol <- grammar == null ? "" : grammar.getModuleSymbol(0);
        valid <- grammar != null && grammar.validate("Wall,Wall");
        report <- grammar == null ? "" : grammar.debugReport();
    )"));

    REQUIRE(vm.find("created").find("ok").toBool());
    REQUIRE(vm.find("isProxy").toBool());
    REQUIRE(vm.find("added").toBool());
    REQUIRE_EQ(vm.find("count").toInt(), 1);
    REQUIRE_EQ(vm.find("symbol").toString(), std::string("Wall"));
    REQUIRE(vm.find("valid").toBool());
    REQUIRE(vm.find("report").toString().find("symbols=") != std::string::npos);
}
}  // namespace test_procgen_scene_sink
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_production)
#include <cstdint>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "production/Production.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// production.cpp
namespace test_production {
using namespace eve::production;


TEST_CASE("production.script.queueLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Production();
        local queueResult = module.newWorkQueue();
        local queue = queueResult.ok ? queueResult.value : null;
        local slots = queue == null ? { ok=false } : queue.setSlotCount("factory", 2);
        local idResult = queue == null ? { ok=false } : queue.enqueue(
            "factory", "vehicle", "tank", "{\"armor\":100}", 2.0, 5);
        local id = idResult.ok ? idResult.value : "";
        local advanced = queue == null ? { ok=false } : queue.advance(1, 2.0);
        local task = queue == null ? null : queue.find(id);
        local savedResult = queue == null ? { ok=false } : queue.snapshot();
        local saved = savedResult.ok ? savedResult.value : "";
        local context = task == null ? {} : task.getContext();
        if (queueResult.ok && slots.ok && idResult.ok && advanced.ok && savedResult.ok && task != null &&
            task.getState() == "completed" && context.armor == 100 &&
            queue.eventAt(queue.eventCount() - 1).getKind() == "completed" && queue.restore(saved).ok) result = "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_production
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_profiler)
#include <string>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Profile.h"
#include "profiler/Profiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
extern const char* profiler_content;
// profiler.cpp
namespace test_profiler {
using namespace eve;


// profiler.cpp
using eve::profiler::Profiler;


TEST_CASE("profiler.core.aggregatesSelfAndTotal") {
    eve::prof::Profiler::reset();
    eve::prof::Profiler::setEnabled(true);
    eve::prof::Profiler::zoneBegin("parent", "physics");
    eve::prof::Profiler::zoneBegin("child", "physics");
    eve::prof::Profiler::zoneEnd();
    eve::prof::Profiler::zoneEnd();
    const double frameMs = eve::prof::Profiler::frameMark();
    CHECK_GT(frameMs, 0.0);
    CHECK(eve::prof::Profiler::hasFrame());
    const auto& samples     = eve::prof::Profiler::lastFrame();
    bool        foundParent = false, foundChild = false;
    for (const auto& s : samples) {
        if (s.name == "parent" && s.module == "physics") {
            foundParent = true;
            // parent self time must be < its total time (a child ran inside it).
            CHECK_LT(s.selfMs, s.totalMs);
        }
        if (s.name == "child") foundChild = true;
    }
    CHECK(foundParent);
    CHECK(foundChild);
    CHECK(eve::prof::Profiler::textReport().find("parent") != std::string::npos);
    eve::prof::Profiler::setEnabled(false);
    eve::prof::Profiler::reset();
}


// profiler_script.cpp

UnitSciptTest(ProfilerScriptTest, profiler_content);

TEST_CASE_FIXTURE(ProfilerScriptTest, "ProfilerScriptTest.capture") { REQUIRE(callScript("captureWorks")); }
}  // namespace test_profiler
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_reload_script)
#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Module.h"
#include "devtools/DevTool.hpp"
#include "devtools/ReloadSession.h"
#include "devtools/Snapshot.hpp"
#include "scripts.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// reload_script.cpp
namespace test_reload_script {
namespace {

/**
 * @brief The production state-migration helpers, taken verbatim from the bootstrap.
 *
 * These used to be a hand-written mirror in this file, which meant the test could keep
 * passing while `load.nut`'s real helper diverged -- and it did: the production version
 * silently copied nothing for a while because `n[k] <- v` throws for a dynamic key on a
 * class instance (plan doc 9.21.1). Slicing the two functions out of `eve::load_content`
 * makes the test execute the same source scripts run, and a rename or a rewrite in
 * load.nut fails the reload cases instead of drifting unnoticed.
 *
 * The whole bootstrap cannot simply be run here: its top level reads config.nut,
 * instantiates modules, opens a window and drives frames.
 */
std::string migrationHelpersSource() {
    const std::string source(eve::load_content);
    const size_t      begin = source.find("function migrate_instance(");
    const size_t      remap = source.find("function remap_instances(");
    if (begin == std::string::npos || remap == std::string::npos || remap < begin) return {};
    const size_t end = source.find("\n}\n", remap);
    if (end == std::string::npos) return {};
    return source.substr(begin, end - begin + 3);
}

const char* kReloadScript = R"(
function reloadProtocolKeepsOldValuesAndNewFields() {
    eve.dev.clearStateRoots();
    getroottable().gameState <- { level = 3, hero = "alice" };
    eve.dev.markStateRoot("gameState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;
    // Simulate a script reload: the definition is replaced by new defaults.
    getroottable().gameState <- { level = 0, hero = "nobody", quest = "intro" };
    err = eve.dev.commitStateReload();
    if (err != "") return false;
    return gameState.level == 3 && gameState.hero == "alice" &&
           gameState.quest == "intro";
}

function reloadProtocolRestoresMissingRoot() {
    eve.dev.clearStateRoots();
    getroottable().gameState <- { level = 7 };
    eve.dev.markStateRoot("gameState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;
    // The new script no longer defines the root.
    delete getroottable().gameState;
    err = eve.dev.commitStateReload();
    if (err != "") return false;
    return ("gameState" in getroottable()) && gameState.level == 7;
}

function reloadAbortRollsBack() {
    eve.dev.clearStateRoots();
    getroottable().gameState <- { hp = 50 };
    eve.dev.markStateRoot("gameState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;
    getroottable().gameState <- { hp = 1 };
    err = eve.dev.abortStateReload();
    if (err != "") return false;
    return gameState.hp == 50;
}

function migrateInstanceCopiesFieldsAndKeepsDefaults() {
    class HeroV1 {
        level = 1;
        name = "?";
    }
    class HeroV2 {
        level = 1;
        name = "?";
        title = "novice";
    }
    local old = HeroV1();
    old.level = 5;
    old.name = "alice";
    local n = migrate_instance(old, HeroV2);
    return n.level == 5 && n.name == "alice" && n.title == "novice";
}

function migrateInstanceFromTable() {
    class HeroV2 {
        level = 1;
        name = "?";
        title = "novice";
    }
    local t = { level = 9, name = "bob" };
    local n = migrate_instance(t, HeroV2);
    return n.level == 9 && n.name == "bob" && n.title == "novice";
}

function remapInstancesContainer() {
    class ItemV1 {
        id = "?";
    }
    class ItemV2 {
        id = "?";
        tag = "new";
    }
    local arr = [ItemV1(), ItemV1()];
    arr[0].id = "a";
    arr[1].id = "b";
    remap_instances(arr, ItemV2);
    return arr[0].id == "a" && arr[0].tag == "new" && arr[1].id == "b" &&
           arr[1].tag == "new";
}

function stateRootsReportsMarkedAndHeuristic() {
    eve.dev.clearStateRoots();
    eve.dev.markStateRoot("customState");
    local roots = eve.dev.stateRoots();
    if (roots.len() != 1 || roots[0] != "customState") return false;

    eve.dev.clearStateRoots();
    getroottable().myGame <- { x = 1 };
    local found = false;
    foreach (r in eve.dev.stateRoots()) {
        if (r == "myGame") found = true;
    }
    return found;
}

function transientRootsAreExcludedFromCapture() {
    eve.dev.clearStateRoots();
    getroottable().scratchState <- { frame = 10 };
    eve.dev.markTransientStateRoot("scratchState");
    foreach (r in eve.dev.stateRoots()) {
        if (r == "scratchState") return false;
    }
    local transient = eve.dev.transientStateRoots();
    return transient.len() == 1 && transient[0] == "scratchState";
}

function abortRestoresStateRootPolicies() {
    eve.dev.clearStateRoots();
    getroottable().savedState <- { value = 1 };
    getroottable().scratchState <- { value = 2 };
    eve.dev.markStateRoot("savedState");
    eve.dev.markTransientStateRoot("scratchState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;

    eve.dev.clearStateRoots();
    eve.dev.markStateRoot("replacementState");
    err = eve.dev.abortStateReload();
    if (err != "") return false;

    local roots = eve.dev.stateRoots();
    local transient = eve.dev.transientStateRoots();
    return roots.len() == 1 && roots[0] == "savedState" &&
           transient.len() == 1 && transient[0] == "scratchState";
}

function commitKeepsNewStateRootPolicies() {
    eve.dev.clearStateRoots();
    getroottable().savedState <- { value = 1 };
    eve.dev.markStateRoot("savedState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;

    eve.dev.clearStateRoots();
    eve.dev.markTransientStateRoot("replacementState");
    err = eve.dev.commitStateReload();
    if (err != "") return false;

    local transient = eve.dev.transientStateRoots();
    return transient.len() == 1 && transient[0] == "replacementState";
}
)";

/** ScriptTest variant that also exposes `eve.dev` (DevTool script API). */
class ReloadScriptTest {
public:
    explicit ReloadScriptTest(const char* script) : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        eve::dev::DevTool::instance().attach(vm);
        eve::dev::DevTool::instance().exposeScriptApi(vm);
        // The production migration helpers first, then the cases that call them.
        const std::string helpersSource = migrationHelpersSource();
        ssq::Script       helpers       = vm.compileSource(helpersSource.c_str());
        vm.run(helpers);
        ssq::Script s = vm.compileSource(script);
        vm.run(s);
    }

    ~ReloadScriptTest() { eve::dev::DevTool::instance().detach(); }

    ssq::VM vm;
};

#define UnitReloadScriptTest(name, content)   \
    class name : public ReloadScriptTest {    \
    public:                                   \
        name() : ReloadScriptTest(content) {} \
    }

UnitReloadScriptTest(ReloadScriptTestFixture, kReloadScript);

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.protocolKeepsOldValuesAndNewFields") {
    REQUIRE(vm.callFunc(vm.findFunc("reloadProtocolKeepsOldValuesAndNewFields"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.protocolRestoresMissingRoot") {
    REQUIRE(vm.callFunc(vm.findFunc("reloadProtocolRestoresMissingRoot"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.abortRollsBack") {
    REQUIRE(vm.callFunc(vm.findFunc("reloadAbortRollsBack"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.migrateInstanceCopiesFieldsAndKeepsDefaults") {
    REQUIRE(vm.callFunc(vm.findFunc("migrateInstanceCopiesFieldsAndKeepsDefaults"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.migrateInstanceFromTable") {
    REQUIRE(vm.callFunc(vm.findFunc("migrateInstanceFromTable"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.remapInstancesContainer") {
    REQUIRE(vm.callFunc(vm.findFunc("remapInstancesContainer"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.stateRootsReportsMarkedAndHeuristic") {
    REQUIRE(vm.callFunc(vm.findFunc("stateRootsReportsMarkedAndHeuristic"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.transientRootsAreExcludedFromCapture") {
    REQUIRE(vm.callFunc(vm.findFunc("transientRootsAreExcludedFromCapture"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.abortRestoresStateRootPolicies") {
    REQUIRE(vm.callFunc(vm.findFunc("abortRestoresStateRootPolicies"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.commitKeepsNewStateRootPolicies") {
    REQUIRE(vm.callFunc(vm.findFunc("commitKeepsNewStateRootPolicies"), vm).toBool());
}

TEST_CASE("reloadScript.migrationHelpersComeFromTheBootstrap") {
    // The migration cases above must exercise load.nut's helpers, not a copy of them:
    // a mirror let them pass while the real helper silently copied nothing (plan doc
    // 9.21.1). This pins the wiring, and a rename in load.nut fails here instead of
    // quietly turning the three migration cases into no-ops.
    const std::string source = migrationHelpersSource();
    REQUIRE(!source.empty());
    REQUIRE(source.rfind("function migrate_instance(", 0) == 0);
    REQUIRE(source.find("function remap_instances(") != std::string::npos);
}

TEST_CASE("reloadScript.embeddedLoadScriptCompiles") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    bool    compiled = false;
    try {
        vm.compileSource(eve::load_content);
        compiled = true;
    } catch (const std::exception& error) {
        // A broken bootstrap has to say what broke; swallowing the text would leave a
        // bare "compiled == false" with no way to tell which line failed.
        std::cerr << "[reloadScript] embedded load_content does not compile: " << error.what() << std::endl;
    }
    REQUIRE(compiled);
}

namespace {

class FailFirstRestoreProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "reload-failure-test"; }

    eve::Result<eve::Value> capture() override { return eve::Result<eve::Value>::success(eve::Value::integer(42)); }

    eve::Result<void> restore(const eve::Value&) override {
        ++restoreCount;
        if (restoreCount == 1)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "intentional commit failure"));
        return eve::Result<void>::success();
    }

    eve::Result<void> resetToDefaults() override { return eve::Result<void>::success(); }

    int restoreCount = 0;
};

class ResetOnReloadProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "reload-reset-test"; }

    eve::caps::StateReloadPolicy reloadPolicy() const override { return eve::caps::StateReloadPolicy::Reset; }

    eve::Result<eve::Value> capture() override {
        ++captureCount;
        return eve::Result<eve::Value>::success(eve::Value::null());
    }

    eve::Result<void> restore(const eve::Value&) override {
        ++restoreCount;
        return eve::Result<void>::success();
    }

    eve::Result<void> resetToDefaults() override {
        ++resetCount;
        return eve::Result<void>::success();
    }

    int captureCount = 0;
    int restoreCount = 0;
    int resetCount   = 0;
};

}  // namespace

TEST_CASE("reloadScript.failedCommitCanStillAbort") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::dev::Snapshot::instance().clearRoots();

    FailFirstRestoreProvider provider;
    eve::cap::addListener<eve::caps::IStateProvider>(&provider);

    auto&       session = eve::dev::ReloadSession::instance();
    std::string err;
    REQUIRE(session.begin(vm.getHandle(), &err));
    REQUIRE(!session.commit(vm.getHandle(), &err));
    REQUIRE_EQ(provider.restoreCount, 2);  // Failed restore plus rollback.

    err.clear();
    REQUIRE(session.abort(vm.getHandle(), &err));
    REQUIRE(err.empty());
    REQUIRE_EQ(provider.restoreCount, 3);

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

TEST_CASE("reloadScript.nativeResetPolicyBacksUpBeforeReset") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::dev::Snapshot::instance().clearRoots();

    ResetOnReloadProvider provider;
    eve::cap::addListener<eve::caps::IStateProvider>(&provider);

    auto&       session = eve::dev::ReloadSession::instance();
    std::string err;
    REQUIRE(session.begin(vm.getHandle(), &err));
    REQUIRE(session.commit(vm.getHandle(), &err));
    REQUIRE_EQ(provider.captureCount, 1);  // Backup at commit; begin still skips Reset providers.
    REQUIRE_EQ(provider.restoreCount, 0);
    REQUIRE_EQ(provider.resetCount, 1);

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

}  // namespace
}  // namespace test_reload_script
#undef UnitReloadScriptTest
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_classic_semantic_index)
#include <filesystem>
#include <fstream>
#include <simplesquirrel/vm.hpp>
#include <sstream>
#include "PathBesideSource.h"
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_classic_semantic_index.cpp
namespace test_rpg_classic_semantic_index {
namespace {
std::string source(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing RPG project input");
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}
}  // namespace
TEST_CASE("rpg.classic.semanticQuestEventSceneRuntime") {
    const auto project =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path() /
        "examples/rpg-classic";
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(source(project / "quest_scene.nut").c_str()));
    vm.run(vm.compileSource(source(project / "tests/quest_scene_probe.nut").c_str()));
    vm.set("questDefinitions", source(project / "data/quests.json"));
    vm.run(vm.compileSource(R"SQ(
        probePassed <- runQuestSceneProbe(questDefinitions);
        probeEventType <- questSceneLog.at(0).getType();
        probeEventSubject <- questSceneLog.at(0).getSubject();
    )SQ"));
    CHECK(vm.get<bool>("probePassed"));
    CHECK_EQ(vm.get<std::string>("probeEventType"), std::string("quest.completed"));
    CHECK_EQ(vm.get<std::string>("probeEventSubject"), std::string("quest.slayer"));
    auto host = eve::scene::Scene::create()->findHost("rpg-classic.quests");
    REQUIRE(host.ok());
    auto marker = host.value()->findById("quest.slayer.marker");
    REQUIRE(marker.ok());
    CHECK(marker.value()->visible);
    // New-game projection must clear the badge without emitting a fake event.
    vm.run(vm.compileSource(R"SQ(
        synchronizeQuestScene(eve.RPG().newTracker());
        probeCleared <- !questSceneMarkerVisible() && questSceneLog.size() == 1;
    )SQ"));
    CHECK(vm.get<bool>("probeCleared"));
    CHECK(!marker.value()->visible);
}
}  // namespace test_rpg_classic_semantic_index
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_dnut_script)
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "rpg/RpgDialect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_dnut_script.cpp
namespace test_rpg_dnut_script {
/**
 * @brief Drive a `.dnut` story from Squirrel with the script owning movement.
 *
 * The story declares *that* an actor moves; the script decides what moving
 * means for its own game. That split is the whole point of the dialect: one
 * authored story survives a tile RPG, a tactics board or a continuous world,
 * because the engine never interprets the coordinates and never grows a
 * movement model of its own.
 *
 * This is also the only end-to-end cover of the `eve.RPG()` story surface:
 * publish -> session -> step stream -> effect.
 */
TEST_CASE("rpg.dnut.scriptOwnsMovementAndVerifiesTheEffect") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        moveRequestSeen <- "";
        local rpg = eve.RPG();
        local source = "story demo.walk {\n"
                     + "    move actor=hero x=3 y=4 duration=0.5\n"
                     + "    variable name=step op=set value=1\n"
                     + "    message text=Arrived\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "demo.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local session = rpg.newStorySession();
            local began = session.begin("demo.walk", gs, null, null, null);
            if (began.ok && session.getStepKind() == "move" &&
                session.getStepString("actor") == "hero" &&
                session.getStepNumber("x") == 3.0 &&
                session.getStepNumber("y") == 4.0 &&
                session.getStepNumber("duration") == 0.5) {
                moveRequestSeen = session.getStepString("actor") + ":" +
                                  session.getStepNumber("x") + "," + session.getStepNumber("y");
                // The script's own movement model runs here. This one is
                // instantaneous; a real game would walk its actor and only
                // acknowledge the step once the actor had actually arrived.
                local advanced = session.advance();
                if (advanced.ok && session.getStepKind() == "message" &&
                    gs.getVariable("step") == 1.0 &&
                    session.getStepString("text") == "Arrived") {
                    local finished = session.advance();
                    if (finished.ok && !session.isActive()) result = "ok";
                }
            }
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
    // Effect verification: the script saw the authored move verbatim.
    CHECK_EQ(vm.find("moveRequestSeen").toString().find("hero:3"), std::size_t(0));

    eve::rpg::RpgStoryCatalogue::clear();
}

/** @brief A story a script never acknowledges stays suspended and keeps its cursor. */
TEST_CASE("rpg.dnut.scriptStaysOnTheMoveStepUntilItAcknowledges") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        local rpg = eve.RPG();
        local source = "story demo.hold {\n"
                     + "    move actor=hero x=1 y=2\n"
                     + "    message text=Arrived\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "hold.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local session = rpg.newStorySession();
            local began = session.begin("demo.hold", gs, null, null, null);
            if (began.ok && session.isActive() && session.isBlocked() &&
                session.getStepKind() == "move" && session.getStoryId() == "demo.hold") {
                // Frame 1 ends here: the host is still walking the actor.
                local stillMoving = session.getStepKind() == "move";
                // Frame 2: the host acknowledges the finished move.
                local advanced = session.advance();
                if (stillMoving && advanced.ok &&
                    session.getStepKind() == "message" &&
                    session.getStepString("text") == "Arrived") {
                    if (session.advance().ok && !session.isActive()) result = "ok";
                }
            }
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    eve::rpg::RpgStoryCatalogue::clear();
}

/** @brief A script sees the compile diagnostic of a broken story, and the old catalogue survives. */
TEST_CASE("rpg.dnut.scriptSeesStructuredFailureForBrokenContent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        local rpg = eve.RPG();
        local good = rpg.replaceStoriesFromDnut("story keep.me {\n    message text=hi\n}\n", "keep.dnut");
        if (good.ok) {
            local bad = rpg.replaceStoriesFromDnut("story broken.me {\n    message\n}\n", "broken.dnut");
            if (!bad.ok && rpg.hasStory("keep.me") && rpg.getStoryCount() == 1) result = "ok";
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    eve::rpg::RpgStoryCatalogue::clear();
}

/**
 * @brief A real party reaches the domain steps through the nullable script surface.
 *
 * `begin` accepts the optional collaborators as Squirrel objects so that `null`
 * is expressible. This case pins the other half of that contract: an actual
 * `RPGParty` still arrives as the C++ type the dialect resolves actors from.
 */
TEST_CASE("rpg.dnut.scriptResolvesActorsThroughARealParty") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        local rpg = eve.RPG();
        local source = "story demo.party {\n"
                     + "    skill actor=hero learn=fireball\n"
                     + "    variable name=gold op=set value=7\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "party.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local party = rpg.newParty();
            local hero = rpg.newActor();
            local added = party.addMember("hero", hero);
            local session = rpg.newStorySession();
            local began = session.begin("demo.party", gs, party, null, null);
            if (added.ok && began.ok && !session.isActive() &&
                hero.knowsSkill("fireball") && gs.getVariable("gold") == 7.0) {
                result = "ok";
            }
            if (hero != null) hero.release();
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    eve::rpg::RpgStoryCatalogue::clear();
}

/**
 * @brief Drive a `.dnut` story from Squirrel with the script owning playback.
 *
 * The sibling of `rpg.dnut.scriptOwnsMovementAndVerifiesTheEffect`: the story
 * declares *that* a clip plays and with which flags, while the script decides
 * what a clip means for its own animation stack. `clip` is never resolved by the
 * engine, so the same authored story survives a skeletal rig, a sprite sequence
 * or a Spine setup.
 *
 * The C++ playback port (`RpgStoryBinding::playAnimation`) is not exposed to
 * Squirrel, exactly like `moveActor`: a script drives playback through the
 * host-presented contract — read the payload, then acknowledge the step.
 */
TEST_CASE("rpg.dnut.scriptOwnsAnimationAndVerifiesTheEffect") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        clipRequestSeen <- "";
        local rpg = eve.RPG();
        local source = "story demo.emote {\n"
                     + "    animation target=hero clip=emote.cheer loop=true hold=false\n"
                     + "    variable name=step op=set value=1\n"
                     + "    message text=Cheered\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "emote.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local session = rpg.newStorySession();
            local began = session.begin("demo.emote", gs, null, null, null);
            if (began.ok && session.getStepKind() == "animation" &&
                session.getStepString("target") == "hero" &&
                session.getStepString("clip") == "emote.cheer" &&
                session.getStepBool("loop") && !session.getStepBool("hold")) {
                clipRequestSeen = session.getStepString("target") + ":" + session.getStepString("clip");
                // The script's own playback model runs here. This one is
                // instantaneous; a real game would start the clip and only
                // acknowledge the step once it had actually finished.
                local advanced = session.advance();
                if (advanced.ok && session.getStepKind() == "message" &&
                    gs.getVariable("step") == 1.0 &&
                    session.getStepString("text") == "Cheered") {
                    local finished = session.advance();
                    if (finished.ok && !session.isActive()) result = "ok";
                }
            }
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
    // Effect verification: the script saw the authored clip request verbatim.
    CHECK_EQ(vm.find("clipRequestSeen").toString(), std::string("hero:emote.cheer"));

    eve::rpg::RpgStoryCatalogue::clear();
}
}  // namespace test_rpg_dnut_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_script)
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_script.cpp
namespace test_rpg_script {
TEST_CASE("rpg.script.beginCastSkillAcceptsNullAndOmittedTarget") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        rpg <- eve.RPG();
        rpg.clearSkillDefinitions();
        rpg.registerSkillsFromJson("[{\"id\":\"script.cast.self\",\"castTime\":0,\"cooldown\":0}]");
        actor <- rpg.newActor();
        actor.learnSkill("script.cast.self");
        omitted <- actor.beginCastSkill("script.cast.self");
        actor.setSkillCooldown("script.cast.self", 0.0);
        withNull <- actor.beginCastSkill("script.cast.self", null);
        other <- rpg.newActor();
        actor.setSkillCooldown("script.cast.self", 0.0);
        withTarget <- actor.beginCastSkill("script.cast.self", other);
    )"));
    CHECK(vm.find("omitted").toBool());
    CHECK(vm.find("withNull").toBool());
    CHECK(vm.find("withTarget").toBool());
}
}  // namespace test_rpg_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_world_state_script)
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "rpg/WorldState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_world_state_script.cpp
namespace test_rpg_world_state_script {
TEST_CASE("rpg.script.worldStateAcceptsStringIdentifiers") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        rpg <- eve.RPG();
        state <- rpg.newGameState();
        world <- rpg.newWorldState(state);
        before <- world.isObjectConsumed("village", "chest");
        consumed <- world.consumeObject("village", "chest");
        after <- world.isObjectConsumed("village", "chest");
        other <- world.isObjectConsumed("forest", "chest");
        reset <- world.resetObject("village", "chest");
        restored <- world.isObjectConsumed("village", "chest");
    )"));
    CHECK(!vm.find("before").toBool());
    CHECK(vm.find("consumed").toTable().find("ok").toBool());
    CHECK(vm.find("after").toBool());
    CHECK(!vm.find("other").toBool());
    CHECK(vm.find("reset").toTable().find("ok").toBool());
    CHECK(!vm.find("restored").toBool());
}
}  // namespace test_rpg_world_state_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_simplesquirrel)
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Squirrel internal headers require this dependency order.
// clang-format off
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqtable.h"
#include "sqclass.h"
// clang-format on
// simplesquirrel.cpp
// Keep VM-private layout with the existing VM-introspection probes; exposing SQVM
// to the binding tests would make pointer RTTI depend on Squirrel's RTTI-disabled VM.
namespace test_simplesquirrel_vm_internals {
TEST_CASE("SimpleSquirrelTest.RemovingAClassFieldFallsThroughToTheMetamethods") {
    // 结论性的一条：把一个已声明的字段从**类**的成员表里摘掉之后，同名成员的读写是否就落到
    // 元方法上？若是，引擎就能在登记组件时"取走字段名与默认值、再把槽位摘掉"，从而既让引擎
    // 持有权威数据，又**不改用户脚本的 `e.pos.x` 写法**。
    ssq::VM vm(128);
    vm.run(vm.compileSource(R"(
        class Probe {
            x = 0.0
            log = ""
            function _get(key) { log = "get:" + key; return 99.0 }
            function _set(key, value) { log = "set:" + key; return value }
        }
        probe <- Probe()
    )"));

    HSQUIRRELVM v   = vm.getHandle();
    const auto  top = sq_gettop(v);
    sq_pushroottable(v);
    sq_pushstring(v, "probe", -1);
    REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
    SQObjectPtr instance;
    sq_getstackobj(v, -1, &instance);
    REQUIRE(sq_gettype(v, -1) == OT_INSTANCE);
    _class(instance)->_members->Remove(SQObjectPtr(SQString::Create(_ss(v), "x")));
    sq_settop(v, top);

    vm.run(vm.compileSource(R"(
        afterRead <- probe.x
        probe.x = 5.0
        afterLog <- probe.log
    )"));

    REQUIRE_EQ(vm.find("afterRead").toFloat(), 99.0);                  // 读到的是元方法给的值
    REQUIRE_EQ(vm.find("afterLog").toString(), std::string("set:x"));  // 写入也走了元方法
}
}  // namespace test_simplesquirrel_vm_internals
#endif
