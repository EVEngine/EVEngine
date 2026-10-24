#include "devtools/McpServer.hpp"
#include "devtools/McpCoreSchemas.hpp"
#include "devtools/McpToolFamilies.hpp"

#include "common/EditorAutomation.h"
#include "common/GameplayControlJson.h"
#include "common/ScriptComponentSnapshot.h"
#include "common/ScriptError.h"
#include "devtools/Immortal.hpp"

#include "devtools/AiPanel.hpp"
#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/GameWorldContext.hpp"
#include "devtools/McpArgs.hpp"
#include "devtools/McpDevBridge.hpp"
#include "devtools/McpFrameCapture.hpp"
#include "devtools/McpInputTools.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpRuntimeTools.hpp"
#include "devtools/McpSceneDirector.hpp"
#include "devtools/McpSceneLiteral.hpp"
#include "devtools/McpScriptTools.hpp"
#include "devtools/McpSquirrelJson.hpp"
#include "devtools/McpTaskProfile.hpp"
#include "devtools/McpToolFilter.hpp"
#include "devtools/PlayHost.h"
#include "devtools/RenderVision.hpp"
#include "devtools/SceneInspect.hpp"
#include "devtools/Snapshot.hpp"

#include "scripts.h"

#include "common/AudioQuery.h"
#include "common/BindingContracts.h"
#include "common/Capability.h"
#include "common/EditorHost.h"
#include "common/Module.h"
#include "common/ParticlesQuery.h"
#include "common/PhysicsQuery.h"
#include "common/PixelWorldAutomation.h"
#include "common/ProcgenQuery.h"
#include "common/RenderCapture.h"
#include "common/SceneQuery.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptError.h"
#include "common/UIAutomation.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include <squirrel.h>
#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace eve::dev {
namespace {

std::string idToJson(const Poco::Dynamic::Var& id) {
    if (id.isEmpty()) return "null";
    try {
        if (id.isInteger() || id.isNumeric()) return std::to_string(id.convert<Poco::Int64>());
    } catch (...) {
    }
    try {
        return std::string("\"") + mcpJsonEscape(id.convert<std::string>()) + "\"";
    } catch (...) {
        return "null";
    }
}

std::string makeResult(const std::string& idJson, const std::string& resultJson) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson + ",\"result\":" + resultJson + "}";
}

std::string makeError(const std::string& idJson, int code, const std::string& message) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson + ",\"error\":{\"code\":" + std::to_string(code) +
           ",\"message\":\"" + mcpJsonEscape(message) + "\"}}";
}

std::string pauseReasonName(PauseReason r) {
    switch (r) {
        case PauseReason::Breakpoint: return "breakpoint";
        case PauseReason::Step: return "step";
        case PauseReason::Exception: return "exception";
        case PauseReason::Snapshot: return "snapshot";
        case PauseReason::PauseKey: return "pause";
        default: return "none";
    }
}

std::string engineStatusJson(const McpServer& mcp) {
    auto&                   dbg = Debugger::instance();
    Poco::JSON::Object::Ptr o   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    o->set("attached", mcpDevAttached());
    o->set("paused", dbg.isPaused());
    o->set("pauseReason", pauseReasonName(dbg.lastPauseReason()));
    const auto& loc = dbg.pauseLocation();
    o->set("source", loc.source);
    o->set("line", loc.line);
    o->set("function", loc.function);
    o->set("mcpPort", mcp.port());
    o->set("mcpConnected", mcp.hasClient());
    switch (mcp.transport()) {
        case McpServer::Transport::Stdio: o->set("transport", "stdio"); break;
        case McpServer::Transport::Tcp: o->set("transport", "tcp"); break;
        default: o->set("transport", "none"); break;
    }
    o->set("dapPort", DebugAdapter::instance().port());
    o->set("gameRoot", mcp.gameRoot());
    o->set("host", eve::cap::query<eve::IEditorHost>() ? eve::cap::query<eve::IEditorHost>()->status()
                                                       : std::string("unavailable"));
    o->set("callgraphEvents", static_cast<int>(mcpCallgraphEvents()));
    o->set("ai", AiPanel::instance().statusLine());
    // The server dies with the process it serves; point the agent at the
    // persistent log that outlives a crash.
    o->set("crashLog", crashLogSummary());
    o->set("scriptTools", scriptToolCount());
    return mcpStringify(Poco::Dynamic::Var(o));
}

glm::vec3 argVec3(Poco::JSON::Object::Ptr args, const char* key, const glm::vec3& def = {}) {
    if (!args || !args->has(key)) return def;
    try {
        auto arr = args->getArray(key);
        if (arr && arr->size() >= 3) {
            return glm::vec3(static_cast<float>(arr->get(0).convert<double>()),
                             static_cast<float>(arr->get(1).convert<double>()),
                             static_cast<float>(arr->get(2).convert<double>()));
        }
    } catch (...) {
    }
    return def;
}

Poco::JSON::Array::Ptr vec3ToArray(const glm::vec3& v) {
    Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arr->add(v.x);
    arr->add(v.y);
    arr->add(v.z);
    return arr;
}

// ============================= Scene Director (AI scene-authoring) =============
// Thin C++ dispatchers over the Squirrel `scene_director` kit live in
// McpSceneDirector.*; Agents build / inspect scenes through eve_scene_* tools.

std::string renderStatusText(eve::IRenderCapture* cap) {
    Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    if (!cap) {
        o->set("error", "Graphics module not available");
        return mcpStringify(Poco::Dynamic::Var(o));
    }
    const eve::RenderStatusInfo s = cap->status();
    o->set("width", s.width);
    o->set("height", s.height);
    o->set("pixelWidth", s.pixelWidth);
    o->set("pixelHeight", s.pixelHeight);
    o->set("had3DThisFrame", s.had3DThisFrame);
    o->set("readbackEnabled", s.readbackEnabled);
    o->set("backend", s.backend);
    o->set("renderFlowEvents", static_cast<int>(DevTool::instance().renderFlow().eventCount()));
    return mcpStringify(Poco::Dynamic::Var(o));
}

eve::IRenderCapture*        mcpCapture() { return eve::cap::query<eve::IRenderCapture>(); }
eve::ISceneQuery*           mcpScene() { return eve::cap::query<eve::ISceneQuery>(); }
eve::IPhysicsQuery*         mcpPhysics() { return eve::cap::query<eve::IPhysicsQuery>(); }
eve::IProcgenQuery*         mcpProcgen() { return eve::cap::query<eve::IProcgenQuery>(); }
eve::IParticlesQuery*       mcpParticles() { return eve::cap::query<eve::IParticlesQuery>(); }
eve::IAudioQuery*           mcpAudio() { return eve::cap::query<eve::IAudioQuery>(); }
eve::IEditorHost*           mcpHost() { return eve::cap::query<eve::IEditorHost>(); }
eve::IUIAutomation*         mcpUI() { return eve::cap::query<eve::IUIAutomation>(); }
eve::IEditorAutomation*     mcpEditor() { return eve::cap::query<eve::IEditorAutomation>(); }
eve::IPixelWorldAutomation* mcpPixelWorld() { return eve::cap::query<eve::IPixelWorldAutomation>(); }

/**
 * @brief Run a pending breakpoint/error vision dump on the game-loop thread.
 *
 * Graphics readback is only safe here, and both transports have to reach this:
 * a stdio host that skipped it would leave a requested dump pending forever.
 */
void runPendingVisionDump() {
    if (!RenderVision::instance().pending()) return;
    auto* cap = mcpCapture();
    if (cap) RenderVision::instance().pollPending(cap, renderStatusText(cap));
}

Poco::JSON::Object::Ptr renderableObservation(eve::IRenderCapture& capture, int entityId, int generation) {
    Poco::JSON::Object::Ptr output = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto                    inspected =
        capture.inspectRenderable3D(static_cast<std::uint32_t>(entityId), static_cast<std::uint32_t>(generation));
    if (!inspected) {
        output->set("status", "stale");
        output->set("entityId", entityId);
        output->set("generation", generation);
        output->set("error", inspected.status().describe());
        return output;
    }
    const auto& info = inspected.value();
    output->set("status", "ok");
    output->set("entityId", info.entityId);
    output->set("generation", info.generation);
    Poco::JSON::Array::Ptr position = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    position->add(info.x);
    position->add(info.y);
    position->add(info.z);
    output->set("position", position);
    Poco::JSON::Array::Ptr tint = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    tint->add(info.tintR);
    tint->add(info.tintG);
    tint->add(info.tintB);
    tint->add(info.tintA);
    output->set("tint", tint);
    output->set("metallic", info.metallic);
    output->set("roughness", info.roughness);
    output->set("parallaxScale", info.parallaxScale);
    output->set("visible", info.visible);
    output->set("receiveLight", info.receiveLight);
    output->set("castShadow", info.castShadow);
    output->set("receiveShadow", info.receiveShadow);
    output->set("hasPackedMaterial", info.hasPackedMaterial);
    output->set("hasTexture", info.hasTexture);
    output->set("hasShader", info.hasShader);
    return output;
}

Poco::JSON::Object::Ptr sceneNodeObservation(eve::ISceneQuery& scene, const std::string& host,
                                             const std::string& node) {
    Poco::JSON::Object::Ptr output = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    output->set("observer", "scene-node");
    output->set("host", host.empty() ? scene.activeHost() : host);
    output->set("node", node);
    if (node.empty()) {
        output->set("status", "not-found");
        output->set("error", "Scene node is missing from the observed host");
        return output;
    }
    auto lookedUp = host.empty() ? scene.getNode(node) : scene.getNodeIn(host, node);
    if (!lookedUp) {
        output->set("status", "not-found");
        output->set("error", "Scene node is missing from the observed host");
        return output;
    }
    const eve::SceneNodeInfo info = std::move(lookedUp).value();
    output->set("status", "ok");
    output->set("id", info.id);
    output->set("name", info.name);
    output->set("path", info.path);
    output->set("visible", info.visible);
    output->set("x", info.x);
    output->set("y", info.y);
    output->set("z", info.z);
    output->set("yaw", info.yaw);
    output->set("pitch", info.pitch);
    output->set("roll", info.roll);
    output->set("sx", info.sx);
    output->set("sy", info.sy);
    output->set("sz", info.sz);
    output->set("parent", info.parent);
    Poco::JSON::Array::Ptr children = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& child : info.children) children->add(child);
    output->set("children", children);
    return output;
}

Poco::JSON::Object::Ptr runtimeObservation(Poco::JSON::Object::Ptr args, std::string* error) {
    const std::string observer = getArgString(args, "observer", "renderable3d");
    if (observer == "renderable3d") {
        auto* capture = mcpCapture();
        if (!capture) {
            if (error) *error = "graphics module not available";
            return nullptr;
        }
        const int entityId   = getArgInt(args, "entityId", -1);
        const int generation = getArgInt(args, "generation", -1);
        if (entityId < 0 || generation < 0) {
            if (error) *error = "entityId and generation are required";
            return nullptr;
        }
        auto output = renderableObservation(*capture, entityId, generation);
        output->set("observer", observer);
        return output;
    }
    if (observer == "scene-node") {
        auto* scene = mcpScene();
        if (!scene) {
            if (error) *error = "scene module not available";
            return nullptr;
        }
        return sceneNodeObservation(*scene, getArgString(args, "host"), getArgString(args, "node"));
    }
    if (error) *error = "unsupported observer: " + observer;
    return nullptr;
}

struct ExpectationMatch {
    bool                     valid    = true;
    double                   maxError = 0.0;
    std::vector<std::string> mismatches;
    std::string              error;
};

bool isJsonObject(const Poco::Dynamic::Var& value) { return value.type() == typeid(Poco::JSON::Object::Ptr); }

bool isJsonArray(const Poco::Dynamic::Var& value) { return value.type() == typeid(Poco::JSON::Array::Ptr); }

void compareExpectation(const Poco::Dynamic::Var& actual, const Poco::Dynamic::Var& expected, const std::string& path,
                        double tolerance, ExpectationMatch* match) {
    if (!match || !match->valid) return;
    if (isJsonObject(expected)) {
        if (!isJsonObject(actual)) {
            match->valid = false;
            match->error = "Expected object at " + path;
            return;
        }
        auto actualObject   = actual.extract<Poco::JSON::Object::Ptr>();
        auto expectedObject = expected.extract<Poco::JSON::Object::Ptr>();
        for (const auto& item : *expectedObject) {
            const std::string childPath = path.empty() ? item.first : path + "." + item.first;
            if (!actualObject->has(item.first)) {
                match->valid = false;
                match->error = "Observed value has no field " + childPath;
                return;
            }
            compareExpectation(actualObject->get(item.first), item.second, childPath, tolerance, match);
        }
        return;
    }
    if (isJsonArray(expected)) {
        if (!isJsonArray(actual)) {
            match->valid = false;
            match->error = "Expected array at " + path;
            return;
        }
        auto actualArray   = actual.extract<Poco::JSON::Array::Ptr>();
        auto expectedArray = expected.extract<Poco::JSON::Array::Ptr>();
        if (actualArray->size() < expectedArray->size()) {
            match->valid = false;
            match->error = "Observed array is shorter than expectation at " + path;
            return;
        }
        for (std::size_t index = 0; index < expectedArray->size(); ++index) {
            compareExpectation(actualArray->get(static_cast<unsigned>(index)),
                               expectedArray->get(static_cast<unsigned>(index)),
                               path + "[" + std::to_string(index) + "]", tolerance, match);
        }
        return;
    }
    if (expected.isNumeric()) {
        if (!actual.isNumeric()) {
            match->valid = false;
            match->error = "Expected numeric value at " + path;
            return;
        }
        const double error = std::abs(actual.convert<double>() - expected.convert<double>());
        match->maxError    = std::max(match->maxError, error);
        if (error > tolerance) match->mismatches.push_back(path);
        return;
    }
    if (expected.isBoolean()) {
        if (!actual.isBoolean()) {
            match->valid = false;
            match->error = "Expected boolean value at " + path;
            return;
        }
        if (actual.convert<bool>() != expected.convert<bool>()) match->mismatches.push_back(path);
        return;
    }
    if (expected.isString()) {
        if (!actual.isString()) {
            match->valid = false;
            match->error = "Expected string value at " + path;
            return;
        }
        if (actual.convert<std::string>() != expected.convert<std::string>()) match->mismatches.push_back(path);
        return;
    }
    if (expected.isEmpty()) {
        if (!actual.isEmpty()) match->mismatches.push_back(path);
        return;
    }
    match->valid = false;
    match->error = "Unsupported expectation value at " + path;
}

Poco::JSON::Object::Ptr expectationValue(const ExpectationMatch& match) {
    Poco::JSON::Object::Ptr output = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    output->set("valid", match.valid);
    output->set("converged", match.valid && match.mismatches.empty());
    output->set("maxError", match.maxError);
    Poco::JSON::Array::Ptr mismatches = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& path : match.mismatches) mismatches->add(path);
    output->set("mismatches", mismatches);
    if (!match.error.empty()) output->set("error", match.error);
    return output;
}

Poco::JSON::Object::Ptr observationEvent(Poco::JSON::Object::Ptr descriptor, Poco::JSON::Object::Ptr observation,
                                         std::string* error) {
    Poco::JSON::Object::Ptr event = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    event->set("observation", observation);
    if (!observation || observation->optValue<std::string>("status", "unavailable") != "ok") {
        event->set("status", "observation-unavailable");
        event->set("converged", false);
        return event;
    }
    if (!descriptor || !descriptor->has("expect")) return event;

    double tolerance = 0.001;
    if (descriptor->has("tolerance")) {
        try {
            tolerance = descriptor->get("tolerance").convert<double>();
        } catch (...) {
            tolerance = -1.0;
        }
    }
    if (!std::isfinite(tolerance) || tolerance < 0.0) {
        if (error) *error = "tolerance must be a finite non-negative number";
        return nullptr;
    }
    const Poco::Dynamic::Var expected = descriptor->get("expect");
    if (!isJsonObject(expected)) {
        if (error) *error = "expect must be a JSON object";
        return nullptr;
    }
    ExpectationMatch match;
    compareExpectation(Poco::Dynamic::Var(observation), expected, "", tolerance, &match);
    if (!match.valid) {
        if (error) *error = match.error;
        return nullptr;
    }
    event->set("expectation", expectationValue(match));
    event->set("converged", match.mismatches.empty());
    event->set("maxError", match.maxError);
    return event;
}

std::string callTool(McpServer& mcp, const std::string& name, Poco::JSON::Object::Ptr args) {
    auto& dbg = Debugger::instance();
    auto& dap = DebugAdapter::instance();

    if (name == "eve_status") return engineStatusJson(mcp);

    if (name == "eve_api_search") {
        const std::string query       = getArgString(args, "query");
        const std::string module      = getArgString(args, "module");
        const std::string scriptClass = getArgString(args, "class");
        int               limit       = getArgInt(args, "limit", 20);
        if (limit < 0) limit = 0;
        const auto hits =
            eve::script::generatedBindingContracts().search(query, module, scriptClass, static_cast<size_t>(limit));
        std::ostringstream json;
        json << "{\"ok\":true,\"schema\":\"eve.binding-api\",\"schemaVersion\":1,\"query\":\"" << mcpJsonEscape(query)
             << "\",\"module\":\"" << mcpJsonEscape(module) << "\",\"class\":\"" << mcpJsonEscape(scriptClass)
             << "\",\"count\":" << hits.size() << ",\"matches\":[";
        for (size_t i = 0; i < hits.size(); ++i) {
            if (i != 0) json << ',';
            json << "{\"score\":" << hits[i].score << ",\"contract\":" << hits[i].contract.toJson() << '}';
        }
        json << "]}";
        return json.str();
    }

    if (name == "eve_api_get") {
        const std::string                   key         = getArgString(args, "key");
        const std::string                   scriptClass = getArgString(args, "class");
        const std::string                   method      = getArgString(args, "method");
        const auto&                         registry    = eve::script::generatedBindingContracts();
        const eve::script::BindingContract* contract    = nullptr;
        std::string                         lookupKey   = key;
        if (!key.empty()) {
            contract = registry.find(key);
            // Agents often write slot.method or module.Class.method; accept those spellings.
            if (!contract) {
                const auto slash = key.find('/');
                if (slash == std::string::npos) {
                    const auto firstDot = key.find('.');
                    if (firstDot != std::string::npos) {
                        lookupKey           = key;
                        lookupKey[firstDot] = '/';
                        contract            = registry.find(lookupKey);
                    }
                }
            }
        } else if (!scriptClass.empty() && !method.empty()) {
            contract = registry.findMethod(scriptClass, method);
        } else if (!method.empty()) {
            contract = registry.findMethod(method);
        } else {
            return "{\"ok\":false,\"error\":\"missing key or method\"}";
        }
        if (!contract) {
            std::string needle = !key.empty() ? key : (!scriptClass.empty() ? scriptClass + "." + method : method);
            for (char& ch : needle) {
                if (ch == '/' || ch == '.') ch = ' ';
            }
            const auto         hits = registry.search(needle, {}, {}, 5);
            std::ostringstream json;
            json << "{\"ok\":false,\"error\":\"binding not found or method name is ambiguous\"";
            if (!hits.empty()) {
                json << ",\"suggestions\":[";
                for (size_t i = 0; i < hits.size(); ++i) {
                    if (i != 0) json << ',';
                    json << "{\"score\":" << hits[i].score << ",\"key\":\"" << mcpJsonEscape(hits[i].contract.key())
                         << "\"}";
                }
                json << "]";
            }
            json << "}";
            return json.str();
        }
        return std::string("{\"ok\":true,\"schema\":\"eve.binding-api\",\"schemaVersion\":1,\"contract\":") +
               contract->toJson() + "}";
    }

    // 引擎侧脚本组件数据的工具视图（J-3 的"引擎工具读得到"）。没有脚本 VM 时是空列表，不是错误。
    if (name == "eve_script_components") {
        auto payload = eve::scriptComponentStatesJson();
        return payload ? std::move(payload).takeValue() : std::string("error: ") + payload.status().describe();
    }

    if (name == "eve_gameplay") {
        if (!args || !args->has("request")) return "error: missing request";
        std::string request;
        try {
            request = mcpStringify(args->get("request"));
        } catch (const std::exception& error) {
            return std::string("error: invalid request: ") + error.what();
        }
        auto response = eve::executeGameplayControlJson(request);
        return response ? std::move(response).takeValue() : std::string("error: ") + response.status().describe();
    }

    if (name == "eve_play") {
        if (!args || !args->has("request")) return "error: missing request";
        std::string request;
        try {
            request = mcpStringify(args->get("request"));
        } catch (const std::exception& error) {
            return std::string("error: invalid request: ") + error.what();
        }
        auto response = executePlayJson(request);
        return response ? std::move(response).takeValue() : std::string("error: ") + response.status().describe();
    }

    if (name.rfind("eve_pixelworld_", 0) == 0) {
        auto* provider = mcpPixelWorld();
        if (!provider) return "{\"ok\":false,\"error\":\"pixelworld module not available\"}";
        return provider->invoke(name.substr(std::string("eve_pixelworld_").size()),
                                mcpStringify(Poco::Dynamic::Var(args)));
    }

    // ============================= Game UI =============================
    if (name == "eve_ui_tree") {
        return mcpUI() ? mcpUI()->tree(getArgString(args, "host")) : "error: ui module not available";
    }
    if (name == "eve_ui_get") {
        return mcpUI() ? mcpUI()->get(getArgString(args, "host"), getArgString(args, "widget"))
                       : "error: ui module not available";
    }
    if (name == "eve_ui_click") {
        return mcpUI() ? mcpUI()->click(getArgString(args, "host"), getArgString(args, "widget"))
                       : "error: ui module not available";
    }

    // ============================= Editor command protocol =============================
    const auto editorInvoke = [&](const char* operation) {
        return mcpEditor() ? mcpEditor()->invoke(operation, mcpStringify(Poco::Dynamic::Var(args)))
                           : std::string("error: editor module not available");
    };
    if (name == "eve_editor_commands") return editorInvoke("commands");
    if (name == "eve_editor_target_list") return editorInvoke("target-list");
    if (name == "eve_editor_target_create") return editorInvoke("target-create");
    if (name == "eve_editor_target_close") return editorInvoke("target-close");
    if (name == "eve_editor_inspect") return editorInvoke("inspect");
    if (name == "eve_editor_plan") return editorInvoke("plan");
    if (name == "eve_editor_commit") return editorInvoke("commit");
    if (name == "eve_editor_execute") return editorInvoke("execute");
    if (name == "eve_editor_observe_start") {
        auto* editor = mcpEditor();
        if (!editor) return "error: editor module not available";
        std::string             error;
        Poco::JSON::Object::Ptr observation = runtimeObservation(args, &error);
        if (!observation) return "error: " + error;
        if (observation->getValue<std::string>("status") != "ok") return mcpStringify(Poco::Dynamic::Var(observation));
        Poco::JSON::Object::Ptr event = observationEvent(args, observation, &error);
        if (!event) {
            Poco::JSON::Object::Ptr invalid = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            invalid->set("status", "invalid-expectation");
            invalid->set("error", error);
            invalid->set("observation", observation);
            return mcpStringify(Poco::Dynamic::Var(invalid));
        }
        Poco::JSON::Object::Ptr request = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        request->set("descriptor", args);
        request->set("event", event);
        return editor->invoke("observe-start", mcpStringify(Poco::Dynamic::Var(request)));
    }
    if (name == "eve_editor_observe_poll") {
        auto* editor = mcpEditor();
        if (!editor) return "error: editor module not available";
        try {
            Poco::JSON::Parser describeParser;
            Poco::Dynamic::Var described =
                describeParser.parse(editor->invoke("observe-describe", mcpStringify(Poco::Dynamic::Var(args))));
            auto describedObject = described.extract<Poco::JSON::Object::Ptr>();
            if (!describedObject || describedObject->optValue<std::string>("status", "") != "applied")
                return mcpStringify(described);
            auto                    descriptor = describedObject->getObject("descriptor");
            std::string             error;
            Poco::JSON::Object::Ptr observation = runtimeObservation(descriptor, &error);
            if (!observation) {
                observation = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
                observation->set("status", "unavailable");
                observation->set("error", error);
            }
            Poco::JSON::Object::Ptr event = observationEvent(descriptor, observation, &error);
            if (!event) {
                event = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
                event->set("observation", observation);
                event->set("status", "expectation-observation-failed");
                event->set("error", error);
            }
            Poco::JSON::Object::Ptr publish = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            publish->set("sessionId", getArgString(args, "sessionId"));
            publish->set("event", event);
            Poco::JSON::Parser publishParser;
            Poco::Dynamic::Var published =
                publishParser.parse(editor->invoke("observe-publish", mcpStringify(Poco::Dynamic::Var(publish))));
            auto publishedObject = published.extract<Poco::JSON::Object::Ptr>();
            if (!publishedObject || publishedObject->optValue<std::string>("status", "") != "applied")
                return mcpStringify(published);
            return editor->invoke("observe-poll", mcpStringify(Poco::Dynamic::Var(args)));
        } catch (const std::exception& exception) {
            return std::string("error: could not poll observation session: ") + exception.what();
        }
    }
    if (name == "eve_editor_observe_close") return editorInvoke("observe-close");
    if (name == "eve_editor_execute_observe") {
        auto* editor = mcpEditor();
        if (!editor) return "error: editor module not available";
        std::string             error;
        Poco::JSON::Object::Ptr before = runtimeObservation(args, &error);
        if (!before) return "error: " + error;
        if (before->getValue<std::string>("status") != "ok") return mcpStringify(Poco::Dynamic::Var(before));

        Poco::JSON::Object::Ptr output = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        output->set("before", before);
        const bool hasExpectation = args && args->has("expect");
        double     tolerance      = 0.001;
        if (args && args->has("tolerance")) {
            try {
                tolerance = args->get("tolerance").convert<double>();
            } catch (...) {
                tolerance = -1.0;
            }
        }
        if (!std::isfinite(tolerance) || tolerance < 0.0) {
            output->set("status", "invalid-expectation");
            output->set("error", "tolerance must be a finite non-negative number");
            return mcpStringify(Poco::Dynamic::Var(output));
        }
        Poco::Dynamic::Var expected;
        if (hasExpectation) {
            expected = args->get("expect");
            if (!isJsonObject(expected)) {
                output->set("status", "invalid-expectation");
                output->set("error", "expect must be a JSON object");
                return mcpStringify(Poco::Dynamic::Var(output));
            }
            ExpectationMatch validation;
            compareExpectation(Poco::Dynamic::Var(before), expected, "", tolerance, &validation);
            if (!validation.valid) {
                output->set("status", "invalid-expectation");
                output->set("expectation", expectationValue(validation));
                output->set("error", validation.error);
                return mcpStringify(Poco::Dynamic::Var(output));
            }
        }
        bool transactionAccepted = false;
        try {
            Poco::JSON::Parser parser;
            Poco::Dynamic::Var transaction =
                parser.parse(editor->invoke("execute", mcpStringify(Poco::Dynamic::Var(args))));
            output->set("transaction", transaction);
            Poco::JSON::Object::Ptr transactionObject = transaction.extract<Poco::JSON::Object::Ptr>();
            transactionAccepted = transactionObject && transactionObject->optValue<bool>("accepted", false);
            output->set("status", transactionAccepted ? "observed" : "transaction-rejected");
        } catch (const std::exception& exception) {
            output->set("status", "failed");
            output->set("error", std::string("Could not parse Editor transaction response: ") + exception.what());
        }
        error.clear();
        Poco::JSON::Object::Ptr after = runtimeObservation(args, &error);
        if (!after) {
            after = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            after->set("status", "unavailable");
            after->set("error", error);
        }
        output->set("after", after);
        if (transactionAccepted && after->getValue<std::string>("status") != "ok")
            output->set("status", "runtime-observation-failed");
        if (hasExpectation && after->getValue<std::string>("status") == "ok") {
            ExpectationMatch match;
            compareExpectation(Poco::Dynamic::Var(after), expected, "", tolerance, &match);
            output->set("expectation", expectationValue(match));
            output->set("converged", match.valid && match.mismatches.empty());
            output->set("maxError", match.maxError);
            if (!match.valid) {
                output->set("status", "expectation-observation-failed");
                output->set("error", match.error);
            }
        } else if (hasExpectation) {
            output->set("converged", false);
        }
        try {
            Poco::JSON::Parser parser;
            Poco::Dynamic::Var editorState =
                parser.parse(editor->invoke("inspect", mcpStringify(Poco::Dynamic::Var(args))));
            output->set("editor", editorState);
        } catch (const std::exception& exception) {
            output->set("editorError", std::string("Could not parse Editor observation: ") + exception.what());
        }
        return mcpStringify(Poco::Dynamic::Var(output));
    }
    if (name == "eve_editor_cancel") return editorInvoke("cancel");
    if (name == "eve_editor_undo") return editorInvoke("undo");
    if (name == "eve_editor_redo") return editorInvoke("redo");
    if (name == "eve_editor_diagnostics") return editorInvoke("diagnostics");

    if (name == "eve_renderable3d_get") {
        auto* capture = mcpCapture();
        if (!capture) return "error: graphics module not available";
        const int entityId   = getArgInt(args, "entityId", -1);
        const int generation = getArgInt(args, "generation", -1);
        if (entityId < 0 || generation < 0) return "error: entityId and generation are required";
        return mcpStringify(Poco::Dynamic::Var(renderableObservation(*capture, entityId, generation)));
    }

    // ============================= Scene / Entity =============================
    if (name == "eve_scene_status") {
        auto*                   scene = mcpScene();
        Poco::JSON::Object::Ptr o     = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!scene) {
            o->set("error", "Scene module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        const std::string host = scene->activeHost();
        if (host.empty()) {
            o->set("activeHost", Poco::Dynamic::Var());
            o->set("nodeCount", 0);
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("activeHost", host);
        o->set("nodeCount", scene->nodeCount());
        o->set("rootId", scene->rootId());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_scene_nodes") {
        auto* scene = mcpScene();
        if (!scene || scene->activeHost().empty()) return "error: no active scene host";
        const int              limit = getArgInt(args, "limit", 500);
        Poco::JSON::Array::Ptr arr   = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& n : scene->nodes(limit)) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", n.id);
            o->set("name", n.name);
            o->set("path", n.path);
            o->set("visible", n.visible);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_scene_node_get") {
        auto* scene = mcpScene();
        if (!scene || scene->activeHost().empty()) return "error: no active scene host";
        const std::string id = getArgString(args, "id");
        if (id.empty()) return "error: node not found: " + id;
        auto lookedUp = scene->getNode(id);
        if (!lookedUp) return "error: node not found: " + id;
        const eve::SceneNodeInfo n = std::move(lookedUp).value();
        Poco::JSON::Object::Ptr  o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("id", n.id);
        o->set("name", n.name);
        o->set("path", n.path);
        o->set("visible", n.visible);
        o->set("x", n.x);
        o->set("y", n.y);
        o->set("z", n.z);
        o->set("yaw", n.yaw);
        o->set("pitch", n.pitch);
        o->set("roll", n.roll);
        o->set("sx", n.sx);
        o->set("sy", n.sy);
        o->set("sz", n.sz);
        o->set("parent", n.parent);
        Poco::JSON::Array::Ptr kids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& c : n.children) kids->add(c);
        o->set("children", kids);
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_scene_node_set") {
        auto* scene = mcpScene();
        if (!scene || scene->activeHost().empty()) return "error: no active scene host";
        const std::string id = getArgString(args, "id");
        if (id.empty()) return "error: node not found: " + id;
        bool changed = false;
        if (args && args->has("x") && args->has("y") && args->has("z")) {
            if (scene->setNodeTransform(id, getArgFloat(args, "x"), getArgFloat(args, "y"), getArgFloat(args, "z")))
                changed = true;
        }
        if (args && args->has("visible")) {
            if (scene->setNodeVisible(id, getArgBool(args, "visible"))) changed = true;
        }
        return changed ? "ok" : "error: node not found: " + id;
    }

    // ============================= Procgen =============================
    if (name == "eve_procgen_recipes") {
        auto* pg = mcpProcgen();
        if (!pg) return "error: Procgen module not available";
        Poco::JSON::Object::Ptr o       = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto                    addList = [&](const char* key, const std::vector<std::string>& items) {
            Poco::JSON::Array::Ptr a = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            for (const auto& it : items) a->add(it);
            o->set(key, a);
        };
        addList("algorithms", pg->algorithms());
        addList("meshRecipes", pg->meshRecipes());
        addList("textureRecipes", pg->textureRecipes());
        addList("pbrRecipes", pg->pbrRecipes());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_procgen_map") {
        auto* pg = mcpProcgen();
        if (!pg) return "error: Procgen module not available";
        const std::string algorithm = getArgString(args, "algorithm");
        if (algorithm.empty()) return "error: missing algorithm";
        std::vector<std::pair<std::string, std::string>> params;
        for (const auto& key : {"roomCount", "roomMin", "roomMax", "corridorWidth", "autotile", "scale", "octaves"}) {
            if (args && args->has(key)) params.emplace_back(key, std::to_string(getArgInt(args, key)));
        }
        if (args && args->has("corridorStyle"))
            params.emplace_back("corridorStyle", getArgString(args, "corridorStyle"));
        auto generated = pg->generateMap(algorithm, getArgInt(args, "width", 32), getArgInt(args, "height", 32),
                                         static_cast<uint32_t>(getArgInt(args, "seed", 0)), params);
        if (!generated) {
            const auto* diag = generated.status().primaryDiagnostic();
            return "error: " + (diag ? diag->message() : generated.status().describe());
        }
        return std::move(generated).value();
    }

    if (name == "eve_procgen_mesh") {
        auto* pg = mcpProcgen();
        if (!pg) return "error: Procgen module not available";
        const std::string recipe = getArgString(args, "recipe");
        if (recipe.empty()) return "error: missing recipe";
        auto built =
            pg->buildMesh(recipe, static_cast<uint32_t>(getArgInt(args, "seed", 0)), getArgInt(args, "width", -1),
                          getArgInt(args, "height", -1), getArgInt(args, "depth", -1));
        if (!built) {
            const auto* diag = built.status().primaryDiagnostic();
            return "error: " + (diag ? diag->message() : built.status().describe());
        }
        const std::string       json = std::move(built).value();
        Poco::JSON::Object::Ptr o    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("recipe", recipe);
        try {
            Poco::JSON::Parser      parser;
            Poco::Dynamic::Var      parsed = parser.parse(json);
            Poco::JSON::Object::Ptr jo     = parsed.extract<Poco::JSON::Object::Ptr>();
            o->set("vertices", jo->get("vertices"));
            o->set("triangles", jo->get("triangles"));
        } catch (...) {
            return json;
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    // ============================= Physics =============================
    if (name == "eve_physics_new_world") {
        auto* ph = mcpPhysics();
        if (!ph) return "error: Physics module not available";
        const int id = ph->newWorld(getArgFloat(args, "gravityX", 0.f), getArgFloat(args, "gravityY", 900.f));
        if (id < 0) return "error: failed to create world";
        float gx = 0.f, gy = 0.f;
        ph->worldGravity(id, &gx, &gy);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("id", id);
        o->set("gravityX", gx);
        o->set("gravityY", gy);
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_physics_list_worlds") {
        auto* ph = mcpPhysics();
        if (!ph) return "error: Physics module not available";
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (int i = 0; i < ph->worldCount(); ++i) {
            float gx = 0.f, gy = 0.f;
            if (!ph->worldGravity(i, &gx, &gy)) continue;
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", i);
            o->set("gravityX", gx);
            o->set("gravityY", gy);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_physics_raycast") {
        auto* ph = mcpPhysics();
        if (!ph) return "error: Physics module not available";
        eve::RayHitInfo h;
        if (!ph->rayCast(getArgInt(args, "world", 0), getArgFloat(args, "x1"), getArgFloat(args, "y1"),
                         getArgFloat(args, "x2"), getArgFloat(args, "y2"), &h))
            return "error: unknown physics world id";
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("hit", h.hit);
        if (h.hit) {
            o->set("bodyId", h.bodyId);
            o->set("x", h.x);
            o->set("y", h.y);
            o->set("normalX", h.normalX);
            o->set("normalY", h.normalY);
            o->set("fraction", h.fraction);
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_physics_remove_world") {
        auto* ph = mcpPhysics();
        if (!ph) return "error: Physics module not available";
        return ph->removeWorld(getArgInt(args, "world", -1)) ? "ok" : "error: unknown physics world id";
    }

    // ============================= Render =============================
    if (name == "eve_render_status") {
        return renderStatusText(mcpCapture());
    }

    if (name == "eve_render_describe") {
        const bool        fresh  = getArgBool(args, "fresh", false);
        const std::string reason = getArgString(args, "reason");
        return RenderVision::instance().describe(mcpCapture(), renderStatusText(mcpCapture()), fresh, reason);
    }

    if (name == "eve_render_vision_config") {
        auto& rv = RenderVision::instance();
        if (args && args->has("baseUrl")) rv.setBaseUrl(getArgString(args, "baseUrl"));
        if (args && args->has("apiKey")) rv.setApiKey(getArgString(args, "apiKey"));
        if (args && args->has("model")) rv.setModel(getArgString(args, "model"));
        if (args && args->has("path")) rv.setPath(getArgString(args, "path"));
        if (args && args->has("timeoutMs")) rv.setTimeoutMs(getArgInt(args, "timeoutMs", 20000));
        return rv.configJson();
    }

    if (name == "eve_screenshot") {
        auto* cap = mcpCapture();
        if (!cap) return "error: Graphics module not available";
        std::string path = getArgString(args, "path");
        if (path.empty()) path = "mcp_screenshot.png";
        // A relative path resolves against the project root: the host process may
        // run from any working directory, and an agent has to be able to read the
        // artifact back from the path it receives.
        if (std::filesystem::path(path).is_relative() && !mcp.gameRoot().empty())
            path = (std::filesystem::path(mcp.gameRoot()) / path).string();
        // Report one separator convention: the project root already uses '/',
        // and agents echo this path back into file tooling.
        for (char& c : path) {
            if (c == '\\') c = '/';
        }

        const eve::RenderStatusInfo status   = cap->status();
        auto                        captured = cap->savePng(path);
        if (!captured) {
            // Enabling readback and reading it back cannot happen inside one
            // presented frame: the swapchain copy is recorded by the next
            // present. Report that as a retryable state instead of a failure so
            // an unattended loop keeps polling instead of giving up.
            const bool              retryable = !status.readbackEnabled;
            const auto*             diag      = captured.status().primaryDiagnostic();
            Poco::JSON::Object::Ptr o =
                mcpFailureObject(retryable ? "no-presented-frame" : "capture-failed",
                                 retryable ? "Wait one rendered frame and call eve_screenshot again"
                                           : "Check eve_render_status / graphics readiness, then retry",
                                 retryable, {"eve_render_status", "eve_screenshot"});
            o->set("reason", retryable ? "no-presented-frame" : "capture-failed");
            o->set("readbackEnabled", status.readbackEnabled);
            o->set("message", diag ? diag->message() : captured.status().describe());
            const std::string payload = mcpStringify(Poco::Dynamic::Var(o));
            return retryable ? payload : "error: " + payload;
        }
        const eve::CaptureImageInfo info = std::move(captured).value();

        std::error_code         sizeError;
        const auto              bytes = std::filesystem::file_size(path, sizeError);
        Poco::JSON::Object::Ptr o     = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("ok", true);
        o->set("path", path);
        o->set("bytes", sizeError ? Poco::Int64(0) : static_cast<Poco::Int64>(bytes));
        o->set("width", info.width);
        o->set("height", info.height);
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    // ============================= Particles / Weather =============================
    if (name == "eve_particles_status") {
        auto*                   part = mcpParticles();
        Poco::JSON::Object::Ptr o    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!part) {
            o->set("error", "Particles module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("emitterCount", part->emitterCount());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_particles_emit") {
        auto* part = mcpParticles();
        if (!part) return "error: Particles module not available";
        float ex = 0.f, ey = 0.f;
        int   cnt = 0;
        if (!part->createEmitter(getArgInt(args, "buffer", 1000), getArgFloat(args, "x"), getArgFloat(args, "y"),
                                 getArgString(args, "preset"), getArgInt(args, "count", 100), &ex, &ey, &cnt))
            return "error: failed to create emitter";
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("x", ex);
        o->set("y", ey);
        o->set("count", cnt);
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    // ============================= Audio =============================
    if (name == "eve_audio_status") {
        auto*                   audio = mcpAudio();
        Poco::JSON::Object::Ptr o     = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (!audio) {
            o->set("error", "Audio module not available");
            return mcpStringify(Poco::Dynamic::Var(o));
        }
        o->set("volume", audio->volume());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_audio_set_volume") {
        auto* audio = mcpAudio();
        if (!audio) return "error: Audio module not available";
        audio->setVolume(getArgFloat(args, "volume", 1.f));
        return "ok";
    }

    if (name == "eve_audio_stop_all") {
        auto* audio = mcpAudio();
        if (!audio) return "error: Audio module not available";
        audio->stopAll();
        return "ok";
    }

    // ============================= Host UI (headless MCP editor host) =====
    if (name == "eve_host_status") {
        return mcpHost() ? mcpHost()->status() : "error: ui module not available";
    }
    if (name == "eve_host_window_open") {
        const std::string title = getArgString(args, "title", "EVEngine AI Host");
        return mcpHost() ? mcpHost()->openWindow(title, getArgInt(args, "width", 1280), getArgInt(args, "height", 800))
                         : "error: ui module not available";
    }
    if (name == "eve_host_window_close") {
        return mcpHost() ? mcpHost()->closeWindow() : "error: ui module not available";
    }
    if (name == "eve_host_window_state") {
        return mcpHost() ? mcpHost()->windowState() : "error: ui module not available";
    }
    if (name == "eve_host_editor_apply") {
        std::string json;
        if (args && args->has("editor")) {
            try {
                Poco::JSON::Object::Ptr o = args->getObject("editor");
                if (o)
                    json = mcpStringify(Poco::Dynamic::Var(o));
                else
                    json = getArgString(args, "editor");
            } catch (...) {
                json = getArgString(args, "editor");
            }
        }
        if (json.empty()) return "error: missing editor";
        return mcpHost() ? mcpHost()->applyEditor(json) : "error: ui module not available";
    }
    if (name == "eve_host_editor_remove") {
        return mcpHost() ? mcpHost()->removeEditor(getArgString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_editor_list") {
        return mcpHost() ? mcpHost()->listEditors() : "error: ui module not available";
    }
    if (name == "eve_host_editor_state") {
        return mcpHost() ? mcpHost()->editorState(getArgString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_editor_set_value") {
        if (!args || !args->has("value")) return "error: missing value";
        return mcpHost() ? mcpHost()->setEditorValue(getArgString(args, "editor"), getArgString(args, "widget"),
                                                     mcpStringify(getArgVar(args, "value")))
                         : "error: ui module not available";
    }
    if (name == "eve_host_editor_save") {
        return mcpHost() ? mcpHost()->saveEditor(getArgString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_editor_unload") {
        return mcpHost() ? mcpHost()->unloadEditor(getArgString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_vm_register") {
        return mcpHost() ? mcpHost()->registerVM(getArgString(args, "name"), getArgString(args, "source"))
                         : "error: ui module not available";
    }
    if (name == "eve_host_vm_unregister") {
        return mcpHost() ? mcpHost()->unregisterVM(getArgString(args, "name")) : "error: ui module not available";
    }
    if (name == "eve_host_events") {
        return mcpHost() ? mcpHost()->consumeEvents(getArgString(args, "editor")) : "error: ui module not available";
    }
    if (name == "eve_host_widget_rect") {
        return mcpHost() ? mcpHost()->widgetRect(getArgString(args, "editor"), getArgString(args, "widget"))
                         : "error: ui module not available";
    }
    if (name == "eve_host_capture") {
        return mcpHost() ? mcpHost()->capture(getArgString(args, "path")) : "error: ui module not available";
    }
    if (name == "eve_host_script") {
        return mcpHost() ? mcpHost()->runScript(getArgString(args, "source")) : "error: ui module not available";
    }
    if (name == "eve_host_resource_reload") {
        return mcpHost() ? mcpHost()->reloadResource(getArgString(args, "path")) : "error: ui module not available";
    }
    if (name == "eve_host_hot_reload_status") {
        return mcpHost() ? mcpHost()->hotReloadStatus() : "error: ui module not available";
    }
    if (name == "eve_host_shutdown") {
        if (mcpHost()) mcpHost()->requestExit();
        return "ok";
    }

    if (name == "eve_eval") {
        const std::string expr = getArgString(args, "expression");
        if (expr.empty()) return "error: missing expression";
        auto                    info = dbg.evaluate(expr);
        Poco::JSON::Object::Ptr o    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("expression", expr);
        o->set("name", info.name);
        o->set("value", info.value);
        o->set("type", info.type);
        o->set("ok", !info.value.empty() || info.type == "null" || !info.name.empty());
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_skeleton_inspect") {
        const std::string actor = getArgString(args, "actor");
        const std::string bone  = getArgString(args, "bone");
        if (actor.empty()) return "error: missing actor";
        const std::string expr =
            "eve_mcp_skeleton_inspect(\"" + mcpJsonEscape(actor) + "\",\"" + mcpJsonEscape(bone) + "\")";
        auto info = dbg.evaluate(expr);
        if (info.value.empty()) return "error: eve_mcp_skeleton_inspect is unavailable";
        // Debugger string values are display-quoted. The project hook returns
        // canonical JSON, so remove only that outer presentation pair.
        if (info.value.size() >= 2 && info.value.front() == '"' && info.value.back() == '"')
            return info.value.substr(1, info.value.size() - 2);
        return info.value;
    }

    if (name == "eve_pause") {
        dbg.pause(PauseReason::PauseKey);
        dap.notifyStopped(PauseReason::PauseKey, dbg.pauseLocation());
        return "paused";
    }
    if (name == "eve_continue") {
        dbg.resume();
        dap.notifyContinued();
        return "continued";
    }
    if (name == "eve_step_over") {
        dbg.stepOver();
        dap.notifyContinued();
        return "step_over";
    }
    if (name == "eve_step_into") {
        dbg.stepInto();
        dap.notifyContinued();
        return "step_into";
    }
    if (name == "eve_step_out") {
        dbg.stepOut();
        dap.notifyContinued();
        return "step_out";
    }
    if (name == "eve_step_frame") {
        dbg.stepFrame();
        dap.notifyContinued();
        return "step_frame";
    }

    if (name == "eve_stack") {
        auto                   frames = dbg.stackTrace();
        Poco::JSON::Array::Ptr arr    = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& f : frames) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", f.id);
            o->set("name", f.name);
            o->set("source", f.loc.source);
            o->set("line", f.loc.line);
            o->set("function", f.loc.function);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_locals") {
        const int              level = getArgInt(args, "level", 1);
        auto                   vars  = dbg.locals(level);
        Poco::JSON::Array::Ptr arr   = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& v : vars) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("name", v.name);
            o->set("value", v.value);
            o->set("type", v.type);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_set_breakpoint") {
        const std::string source = getArgString(args, "source");
        const int         line   = getArgInt(args, "line");
        if (source.empty() || line <= 0) return "error: need source and line";
        const std::string condition = getArgString(args, "condition");
        const int         id        = dbg.setBreakpoint(source, line, true, condition);
        return "ok id=" + std::to_string(id);
    }
    if (name == "eve_clear_breakpoint") {
        const std::string source = getArgString(args, "source");
        const int         line   = getArgInt(args, "line");
        if (source.empty() || line <= 0) return "error: need source and line";
        return dbg.clearBreakpoint(source, line) ? "cleared" : "not_found";
    }
    if (name == "eve_list_breakpoints") {
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& bp : dbg.breakpoints()) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("id", bp.id);
            o->set("source", bp.source);
            o->set("line", bp.line);
            o->set("enabled", bp.enabled);
            o->set("condition", bp.condition);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_watch_add") {
        const std::string expr = getArgString(args, "expression");
        if (expr.empty()) return "error: missing expression";
        dbg.addWatch(expr);
        dbg.refreshWatches();
        return "ok";
    }
    if (name == "eve_watch_list") {
        dbg.refreshWatches();
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& w : dbg.watches()) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("expression", w.expression);
            o->set("value", w.value);
            o->set("ok", w.ok);
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_snapshot_capture") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        std::string json = Snapshot::instance().capture(vm, &err);
        if (!err.empty() && json.empty()) return "error: " + err;
        return json;
    }
    if (name == "eve_snapshot_restore") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string json = getArgString(args, "json");
        std::string       err;
        if (!Snapshot::instance().restore(vm, json, &err)) return "error: " + err;
        return "ok";
    }
    if (name == "eve_snapshot_save") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string path = getArgString(args, "path");
        std::string       err;
        if (!Snapshot::instance().saveFile(vm, path, &err)) return "error: " + err;
        return "ok";
    }
    if (name == "eve_snapshot_load") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string path = getArgString(args, "path");
        std::string       err;
        if (!Snapshot::instance().loadFile(vm, path, &err)) return "error: " + err;
        return "ok";
    }

    if (name == "eve_error_slice") {
        const std::string& last = mcpLastReport();
        if (!last.empty()) return last;
        return "no prior error";
    }

    if (name == "eve_run_script") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string source = getArgString(args, "source");
        if (source.empty()) return "error: missing source";
        std::string error;
        if (!runMcpVmSnippet(vm, source, &error)) return "error: " + error;
        return "ok";
    }

    if (name == "eve_ai_note") {
        const std::string text = getArgString(args, "text");
        if (text.empty()) return "error: missing text";
        AiPanel::instance().addNote(text);
        return "ok";
    }
    if (name == "eve_ai_log") return AiPanel::instance().formatLog(100);

    // ===================== Scene Director (AI scene-authoring) =====================
    // Agent-drivable scene construction backed by the `scene_director` script kit.
    if (name == "eve_scene_director_install") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        // Explicit install is allowed to do heavy work; agents that hit other scene tools
        // first get deferred-to-update instead of blocking JSON-RPC (EM1).
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/true)) return sceneDirectorToolError(name, err);
        return "ok";
    }

    if (name == "eve_scene_director_status") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) return sceneDirectorToolError(name, err);
        return callSceneDirectorReturn(vm, "return ::scene_director.status();", &err);
    }

    if (name == "eve_scene_reset") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) return sceneDirectorToolError(name, err);
        const std::string out = callSceneDirectorReturn(vm, "return ::scene_director.reset();", &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    if (name == "eve_scene_modify") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) return sceneDirectorToolError(name, err);
        const std::string  action = getArgString(args, "action");
        const std::string  target = getArgString(args, "target");
        Poco::Dynamic::Var paramsVar;
        if (args && args->has("params")) {
            try {
                paramsVar = Poco::Dynamic::Var(args->getObject("params"));
            } catch (...) {
                return sceneDirectorToolError(name, "params must be a JSON object");
            }
        }
        auto actionLiteral = sceneLiteral(Poco::Dynamic::Var(action));
        if (!actionLiteral.ok()) return sceneDirectorToolError(name, actionLiteral.status().describe());
        auto targetLiteral = sceneLiteral(Poco::Dynamic::Var(target));
        if (!targetLiteral.ok()) return sceneDirectorToolError(name, targetLiteral.status().describe());
        auto paramsLiteral = sceneLiteral(paramsVar);
        if (!paramsLiteral.ok()) return sceneDirectorToolError(name, paramsLiteral.status().describe());
        const std::string snippet = "return ::scene_director.modify(" + actionLiteral.value() + "," +
                                    targetLiteral.value() + "," + paramsLiteral.value() + ");";
        const std::string out = callSceneDirectorReturn(vm, snippet, &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    // Batch white-box authoring: many scene_director.modify calls in one round-trip.
    if (name == "eve_scene_batch") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) return sceneDirectorToolError(name, err);
        if (!args || !args->has("actions")) return sceneDirectorToolError(name, "actions array is required");
        Poco::Dynamic::Var actionsVar;
        try {
            actionsVar = Poco::Dynamic::Var(args->getArray("actions"));
        } catch (...) {
            return sceneDirectorToolError(name, "actions must be a JSON array");
        }
        Poco::JSON::Object::Ptr options = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        if (args->has("stopOnError")) {
            try {
                options->set("stopOnError", args->get("stopOnError").convert<bool>());
            } catch (...) {
                return sceneDirectorToolError(name, "stopOnError must be a boolean");
            }
        } else {
            options->set("stopOnError", true);
        }
        auto actionsLiteral = sceneLiteral(actionsVar);
        if (!actionsLiteral.ok()) return sceneDirectorToolError(name, actionsLiteral.status().describe());
        auto optionsLiteral = sceneLiteral(Poco::Dynamic::Var(options));
        if (!optionsLiteral.ok()) return sceneDirectorToolError(name, optionsLiteral.status().describe());
        const std::string snippet =
            "return ::scene_director.batch(" + actionsLiteral.value() + "," + optionsLiteral.value() + ");";
        const std::string out = callSceneDirectorReturn(vm, snippet, &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    if (name == "eve_camera_generate") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) return sceneDirectorToolError(name, err);
        const int         count   = getArgInt(args, "count", 6);
        const std::string snippet = "return ::scene_director.cameras(" + std::to_string(count) + ");";
        const std::string out     = callSceneDirectorReturn(vm, snippet, &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    if (name == "eve_scene_info") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/false)) return sceneDirectorToolError(name, err);
        const std::string out = callSceneDirectorReturn(vm, "return ::scene_director.info();", &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    // ---- 场景巡检工具集（图像与 3D 几何数据严格同步） ----
    if (name == "inspect_generate_scene_camera_views") {
        const glm::vec3         center = argVec3(args, "center");
        const float             fov    = getArgFloat(args, "fov", 60.f);
        auto                    views  = SceneInspect::instance().generateViews(center, fov);
        Poco::JSON::Object::Ptr root   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        Poco::JSON::Array::Ptr  arr    = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (const auto& v : views) {
            Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            o->set("name", v.name);
            o->set("kind", v.kind);
            o->set("eye", vec3ToArray(v.eye));
            o->set("target", vec3ToArray(v.target));
            o->set("fov", static_cast<double>(v.fovYDeg));
            arr->add(o);
        }
        root->set("views", arr);
        return mcpStringify(Poco::Dynamic::Var(root));
    }

    if (name == "set_camera_pose") {
        const glm::vec3         pos = argVec3(args, "pos", glm::vec3(0.f, 1.8f, 0.f));
        const glm::vec3         rot = argVec3(args, "rot", glm::vec3(0.f));
        const float             fov = getArgFloat(args, "fov", 0.f);
        const bool              ok  = SceneInspect::instance().setCameraPose(pos, rot, fov);
        Poco::JSON::Object::Ptr o   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("ok", ok);
        if (!ok) {
            o->set("error", "failed to set camera pose (no Graphics/Camera3D)");
        } else {
            try {
                Poco::JSON::Parser parser;
                o->set("pose", parser.parse(SceneInspect::instance().currentPoseJson()));
            } catch (...) {
            }
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "capture_render_frame") {
        const std::string        dir = getArgString(args, "dir");
        const std::string        tag = getArgString(args, "tag", "frame");
        std::vector<std::string> buffers;
        if (args && args->has("buffers")) {
            try {
                auto arr = args->getArray("buffers");
                if (arr) {
                    for (size_t i = 0; i < arr->size(); ++i)
                        buffers.push_back(arr->get(static_cast<unsigned int>(i)).convert<std::string>());
                }
            } catch (...) {
            }
        }
        const auto              res = SceneInspect::instance().capture(dir, tag, buffers);
        Poco::JSON::Object::Ptr o   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("ok", res.ok);
        if (res.ok) {
            o->set("png", res.pngPath);
            o->set("json", res.jsonPath);
            o->set("width", res.width);
            o->set("height", res.height);
            o->set("entityCount", res.entityCount);
            if (!res.depthPngPath.empty()) o->set("depthPng", res.depthPngPath);
            if (!res.normalPngPath.empty()) o->set("normalPng", res.normalPngPath);
            if (!res.idPngPath.empty()) o->set("idPng", res.idPngPath);
            if (!res.idJsonPath.empty()) o->set("idJson", res.idJsonPath);
            if (!res.unsupported.empty()) {
                Poco::JSON::Array::Ptr uns = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
                for (const auto& u : res.unsupported) uns->add(u);
                o->set("unsupported", uns);
            }
        } else {
            o->set("error", res.error);
        }
        return mcpStringify(Poco::Dynamic::Var(o));
    }

    if (name == "get_visible_entities_screen_bbox") {
        const bool hasPos    = args && args->has("pos");
        const bool hasTarget = args && args->has("target");
        if (hasPos && hasTarget) {
            const glm::vec3 eye = argVec3(args, "pos");
            const glm::vec3 tgt = argVec3(args, "target");
            const float     fov = getArgFloat(args, "fov", 0.f);
            return SceneInspect::instance().visibleEntitiesJson(&eye, &tgt, fov);
        }
        const float fov = getArgFloat(args, "fov", 0.f);
        return SceneInspect::instance().visibleEntitiesJson(nullptr, nullptr, fov);
    }

    return "error: unknown tool " + name;
}

// Revisions this server actually implements. The newline-delimited JSON-RPC
// shape and the tools/resources/prompts surface are shared by all of them, so a
// client asking for one of these may keep its own framing assumptions.
constexpr const char* kMcpProtocolLatest = "2025-06-18";

bool isSupportedProtocol(const std::string& requested) {
    return requested == "2025-06-18" || requested == "2025-03-26" || requested == "2024-11-05";
}

std::string handleInitialize(McpServer& mcp, const std::string& idJson, Poco::JSON::Object::Ptr params) {
    std::string clientName = "mcp-client";
    std::string requested  = kMcpProtocolLatest;
    if (params) {
        try {
            if (params->has("clientInfo")) {
                auto info = params->getObject("clientInfo");
                if (info && info->has("name")) clientName = info->get("name").convert<std::string>();
            }
            if (params->has("protocolVersion")) requested = params->get("protocolVersion").convert<std::string>();
        } catch (...) {
        }
    }
    (void)mcp;
    AiPanel::instance().setClientName(clientName);
    AiPanel::instance().addLog("system", "mcp.initialize", clientName);

    // The server must answer with a revision it implements; echoing an
    // arbitrary client string would claim support for a protocol that was never
    // built, and the client would then frame requests accordingly.
    const std::string protocol = isSupportedProtocol(requested) ? requested : kMcpProtocolLatest;

    // Hand-built JSON keeps initialize compact (newline framing) without a Poco Object tree.
    const std::string resultJson =
        std::string("{\"protocolVersion\":\"") + mcpJsonEscape(protocol) +
        "\",\"capabilities\":{\"tools\":{},\"resources\":{},\"prompts\":{}},"
        "\"serverInfo\":{\"name\":\"evengine\",\"title\":\"EVEngine MCP\",\"version\":\"0.6.1\"},"
        "\"instructions\":\"EVEngine MCP for AI-assisted game development. Call eve_api_search/eve_api_get to look up "
        "this build's EveScript bindings before writing .nut (do not invent Unity C# names); prefer eve_play and "
        "editor commands over eve_eval for gameplay. Diagnose through the game's own channels: "
        "eve_console_read returns the runtime console (Squirrel print/error plus engine log lines) with a monotonic "
        "seq cursor, eve_error_slice returns the last script/render error slice, and eve_render_status with "
        "eve_screenshot/eve_screenshot_image capture engine-owned frames. Drive a run with eve_pause/eve_step_* or "
        "eve_play (clock/step/observe/capture/checkpoint/act), assert with eve_eval/eve_run_script and snapshot "
        "tools, and inspect live state with eve_editor_*/eve_scene_*/eve_ui_*. Use eve_restart (optional args) to "
        "soft-restart the game without reloading the resource cache — do not kill the process between AI debug "
        "iterations. eve_host_* tools create JSON-defined editor windows bound to Squirrel ViewModels (MVVM) for "
        "AI-crafted terrain/material/event editors.\"}";
    return makeResult(idJson, resultJson);
}

std::string handleToolsList(const std::string& idJson) {
    // Complete tool objects, comma-separated; the array wrapper is added by
    // handleToolsList so project-exported tools can be appended last.
    const std::string kToolsBody(compiledMcpToolCatalog().schemas());

    // Project-exported tools come last and are re-read every call: a script may
    // register or drop one at any time (hot reload of mcp.nut).
    std::string       body     = kToolsBody;
    const std::string scripted = scriptToolSchemas();
    if (!scripted.empty()) {
        body += ',';
        body += scripted;
    }
    // Toolset / read-only / exclude filters apply to both built-ins and script tools
    // so tools/list and tools/call stay consistent.
    body              = filterMcpToolsBody(body);
    std::string tools = "{\"tools\":[";
    tools += body;
    tools += "]}";
    return makeResult(idJson, tools);
}

std::string handleToolsCall(McpServer& mcp, const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("name")) return makeError(idJson, -32602, "tools/call requires params.name");
    std::string name;
    try {
        name = params->get("name").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "tools/call params.name must be a string");
    }
    Poco::JSON::Object::Ptr args = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    if (params->has("arguments")) {
        try {
            args = params->getObject("arguments");
        } catch (...) {
        }
    }

    std::string detail;
    try {
        if (args) detail = mcpStringify(Poco::Dynamic::Var(args));
    } catch (...) {
    }
    AiPanel::instance().addLog("tool", name, detail);

    if (!mcpToolAllowed(name)) {
        return makeResult(idJson, textContentResult(mcpToolFilteredPayload(name), true));
    }
    mcpFilterNoteCall(true, 0, false);

    if (const auto route = compiledMcpToolCatalog().find(name)) {
        try {
            const auto payload = route->call(mcp, name, args);
            return makeResult(idJson, route->payload == McpToolPayload::ContentEnvelope
                                          ? payload
                                          : textContentResult(payload, payload.rfind("error:", 0) == 0));
        } catch (const std::exception& e) {
            AiPanel::instance().addLog("error", name, e.what());
            return makeResult(idJson, textContentResult(std::string("error: ") + e.what(), true));
        }
    }

    // Project-exported script tools. Built-ins win: a script tool can never
    // shadow a compiled tool, which is why registration rejects their prefixes.
    if (isScriptTool(name)) {
        try {
            return makeResult(idJson, callScriptTool(name, args));
        } catch (const std::exception& e) {
            AiPanel::instance().addLog("error", name, e.what());
            return makeResult(idJson, textContentResult(std::string("error: ") + e.what(), true));
        }
    }

    try {
        const std::string out   = callTool(mcp, name, args);
        const bool        isErr = out.rfind("error:", 0) == 0;
        return makeResult(idJson, textContentResult(out, isErr));
    } catch (const std::exception& e) {
        AiPanel::instance().addLog("error", name, e.what());
        return makeResult(idJson, textContentResult(std::string("error: ") + e.what(), true));
    }
}

std::string handleResourcesList(const std::string& idJson) {
    return makeResult(idJson,
                      "{\"resources\":["
                      "{\"uri\":\"eve://status\",\"name\":\"status\",\"description\":\"Debugger / MCP / DAP "
                      "status\",\"mimeType\":\"application/json\"},"
                      "{\"uri\":\"eve://game-world\",\"name\":\"game-world\",\"description\":\"What kind of game "
                      "world is running at runtime: script publishWorldContext, live domains, and configured "
                      "/ instantiated modules\",\"mimeType\":\"application/json\"},"
                      "{\"uri\":\"eve://error-report\",\"name\":\"error-report\",\"description\":\"Last DevTools error "
                      "slice report\",\"mimeType\":\"text/plain\"},"
                      "{\"uri\":\"eve://ai-session\",\"name\":\"ai-session\",\"description\":\"AI / MCP session "
                      "log\",\"mimeType\":\"text/plain\"},"
                      "{\"uri\":\"eve://callgraph\",\"name\":\"callgraph\",\"description\":\"CallGraph event "
                      "summary\",\"mimeType\":\"application/json\"},"
                      "{\"uri\":\"eve://crash-log\",\"name\":\"crash-log\",\"description\":\"Persistent crash/error "
                      "log (eve.log) that survives a process death\",\"mimeType\":\"text/plain\"}"
                      "]}");
}

std::string handleResourcesRead(const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("uri")) return makeError(idJson, -32602, "resources/read requires params.uri");
    std::string uri;
    try {
        uri = params->get("uri").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "resources/read params.uri must be a string");
    }
    std::string text;
    std::string mime = "text/plain";
    if (uri == "eve://status") {
        text = engineStatusJson(McpServer::instance());
        mime = "application/json";
    } else if (uri == "eve://game-world") {
        auto snapshot = liveGameWorldSnapshot();
        if (!snapshot) {
            const auto* diag = snapshot.status().primaryDiagnostic();
            return makeError(
                idJson, -32002,
                diag && !diag->message().empty() ? diag->message() : std::string("game world snapshot failed"));
        }
        auto json = snapshot.value().toJson();
        if (!json) {
            const auto* diag = json.status().primaryDiagnostic();
            return makeError(idJson, -32002,
                             diag && !diag->message().empty() ? diag->message() : "game world JSON failed");
        }
        text = json.value();
        mime = "application/json";
    } else if (uri == "eve://error-report") {
        text = mcpLastReport();
        if (text.empty()) text = "(no error report yet)\n";
    } else if (uri == "eve://ai-session") {
        text = AiPanel::instance().formatLog(200);
        if (text.empty()) text = "(empty)\n";
    } else if (uri == "eve://callgraph") {
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("events", static_cast<int>(mcpCallgraphEvents()));
        o->set("stackDepth", static_cast<int>(mcpCallgraphStackDepth()));
        text = mcpStringify(Poco::Dynamic::Var(o));
        mime = "application/json";
    } else if (uri == "eve://crash-log") {
        // Same evidence eve_crash_report exposes, as a resource so a client can
        // attach it without a tool round trip. Bounded to the tail: a resource
        // read must not turn a long log into one enormous MCP frame.
        constexpr std::streamoff kCrashResourceBytes = 64 * 1024;
        text                                         = "(no crash log yet)\n";
        const std::string path                       = crashLogSummary()->getValue<std::string>("path");
        std::error_code   exists                     = std::error_code();
        if (!path.empty() && std::filesystem::exists(path, exists)) {
            std::ifstream in(path, std::ios::binary);
            if (in) {
                in.seekg(0, std::ios::end);
                const std::streamoff size  = in.tellg();
                const std::streamoff start = size > kCrashResourceBytes ? size - kCrashResourceBytes : 0;
                in.seekg(start < 0 ? 0 : start);
                std::ostringstream buffer;
                buffer << in.rdbuf();
                text = buffer.str();
                if (start > 0) {
                    const auto newline = text.find('\n');
                    text.erase(0, newline == std::string::npos ? text.size() : newline + 1);
                    text = "[…truncated; full log at " + path + "]\n" + text;
                }
                if (text.empty()) text = "(crash log is empty)\n";
            }
        }
    } else {
        return makeError(idJson, -32002, "Unknown resource: " + uri);
    }

    Poco::JSON::Object::Ptr contentsItem = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    contentsItem->set("uri", uri);
    contentsItem->set("mimeType", mime);
    contentsItem->set("text", text);
    Poco::JSON::Array::Ptr contents = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    contents->add(contentsItem);
    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("contents", contents);
    return makeResult(idJson, mcpStringify(Poco::Dynamic::Var(result)));
}

std::string handlePromptsList(const std::string& idJson) {
    return makeResult(
        idJson,
        "{\"prompts\":["
        "{\"name\":\"debug_failure\",\"description\":\"Investigate the latest script/render error using MCP tools and "
        "the error slice.\"},"
        "{\"name\":\"test_scenario\",\"description\":\"Drive a reproducible in-engine test: pause, snapshot, eval "
        "assertions, continue.\"},"
        "{\"name\":\"ai_game_review\",\"description\":\"Review live game state for AI-generated content issues.\"}"
        "]}");
}

std::string handlePromptsGet(const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("name")) return makeError(idJson, -32602, "prompts/get requires params.name");
    std::string name;
    try {
        name = params->get("name").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "prompts/get params.name must be a string");
    }
    std::string text;
    if (name == "debug_failure") {
        text =
            "You are debugging an EVEngine game via MCP.\n"
            "1) Call eve_status and read resource eve://error-report.\n"
            "2) Use eve_stack / eve_locals / eve_eval to inspect state.\n"
            "3) Use eve_error_slice to narrow script/render causes.\n"
            "4) Propose a minimal fix; verify with eve_run_script or snapshot restore.";
    } else if (name == "test_scenario") {
        text =
            "Design a short automated test against the live EVEngine session:\n"
            "1) Call eve_game_world (or read eve://game-world) to learn genre/systems, then eve_play op=status.\n"
            "2) Observe declared script roots from game.agent.json; do not invent observation paths.\n"
            "3) Pause, step host frames, capture an engine screenshot, and checkpoint restore with eve_play.\n"
            "4) Use eve_gameplay for player-equivalent domain commands when the contract declares a gameplay-domain.\n"
            "5) Do not use eve_eval as the official observation path.";
    } else if (name == "ai_game_review") {
        text =
            "Review this AI-generated or AI-assisted game build:\n"
            "1) eve_game_world + eve_status + eve_ai_log to learn the world and recent agent actions.\n"
            "2) Spot-check critical roots with eve_play observe (or eve_eval only as a debug escape hatch).\n"
            "3) If unstable, pause and capture a snapshot.\n"
            "4) Summarize risks and suggested script fixes.";
    } else {
        return makeError(idJson, -32602, "Unknown prompt: " + name);
    }

    Poco::JSON::Object::Ptr msg = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    msg->set("role", "user");
    Poco::JSON::Array::Ptr  content = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr part    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    part->set("type", "text");
    part->set("text", text);
    content->add(part);
    msg->set("content", content);
    Poco::JSON::Array::Ptr messages = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    messages->add(msg);
    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("description", name);
    result->set("messages", messages);
    return makeResult(idJson, mcpStringify(Poco::Dynamic::Var(result)));
}

std::string callCoreTool(McpServer& server, std::string_view name, Poco::JSON::Object::Ptr args) {
    return callTool(server, std::string(name), std::move(args));
}

}  // namespace

EVE_MCP_TOOL_FAMILY(Core, mcpCoreToolSchemas, callCoreTool, TextValue)

McpServer& McpServer::instance() {
    // Process-immortal singleton: the stdio reader thread (detached when
    // reading stdin) and cross-singleton calls make destruction unsafe; see
    // devtools/Immortal.hpp.
    return Immortal<McpServer>::get();
}

McpServer::McpServer() = default;

McpServer::~McpServer() {
    // Only reached if someone deletes the instance; keep sockets tidy without
    // touching AiPanel (may already be torn down in other lifetime models).
    std::lock_guard<std::mutex> lock(ioMu_);
    if (client_) {
        try {
            client_->close();
        } catch (...) {
        }
        client_.reset();
    }
    if (server_) {
        try {
            server_->close();
        } catch (...) {
        }
        server_.reset();
    }
    stdioQueue_.clear();
    listening_.store(false);
    hasClient_.store(false);
    port_.store(0);
    transport_.store(Transport::None);
}

void McpServer::setGameRoot(std::string root) {
    for (char& c : root) {
        if (c == '\\') c = '/';
    }
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    gameRoot_ = std::move(root);
}

int McpServer::listen(uint16_t port) {
    stop();
    try {
        Poco::Net::SocketAddress addr("127.0.0.1", port);
        server_ = std::make_unique<Poco::Net::ServerSocket>(addr);
        server_->setBlocking(false);
        const int bound = static_cast<int>(server_->address().port());
        port_.store(bound);
        listening_.store(true);
        initialized_ = false;
        try {
            if (gameRoot_.empty()) setGameRoot(std::filesystem::current_path().string());
        } catch (...) {
        }
        AiPanel::instance().setMcpPort(bound);
        AiPanel::instance().setMcpConnected(false);
        AiPanel::instance().addLog("system", "mcp.listen", "listening on 127.0.0.1:" + std::to_string(bound));
        transport_.store(Transport::Tcp);
        return bound;
    } catch (...) {
        server_.reset();
        listening_.store(false);
        port_.store(0);
        transport_.store(Transport::None);
        AiPanel::instance().setMcpPort(0);
        return 0;
    }
}

bool McpServer::listenStdio(std::istream& in, std::ostream& out) {
    stop();
    {
        std::lock_guard<std::mutex> lock(ioMu_);
        stdioQueue_.clear();
    }
    transport_.store(Transport::Stdio);
    listening_.store(true);
    port_.store(0);
    initialized_ = false;
    stdinClosed_.store(false);
    resetMcpFrameCapture();
    hasClient_.store(true);
    stdioIn_    = &in;
    stdioOut_   = &out;
    joinReader_ = (&in != &std::cin);
    try {
        stdioReader_ = std::thread([this, &in]() {
            std::string line;
            while (listening_.load() && std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                {
                    std::lock_guard<std::mutex> lock(ioMu_);
                    stdioQueue_.push_back(std::move(line));
                }
            }
            stdinClosed_.store(true);
            hasClient_.store(false);
        });
    } catch (...) {
        transport_.store(Transport::None);
        listening_.store(false);
        hasClient_.store(false);
        return false;
    }
    AiPanel::instance().setMcpPort(0);
    AiPanel::instance().setMcpConnected(true);
    AiPanel::instance().addLog("system", "mcp.listen", "stdio transport ready (newline JSON-RPC)");
    return true;
}

void McpServer::stop() {
    listening_.store(false);
    hasClient_.store(false);
    if (stdioReader_.joinable()) {
        if (joinReader_) {
            stdioReader_.join();
        } else {
            stdioReader_.detach();
        }
    }
    std::lock_guard<std::mutex> lock(ioMu_);
    if (client_) {
        try {
            client_->close();
        } catch (...) {
        }
        client_.reset();
    }
    if (server_) {
        try {
            server_->close();
        } catch (...) {
        }
        server_.reset();
    }
    stdioQueue_.clear();
    recvBuf_.clear();
    port_.store(0);
    transport_.store(Transport::None);
    initialized_ = false;
    stdioIn_     = nullptr;
    stdioOut_    = nullptr;
    AiPanel::instance().setMcpPort(0);
    AiPanel::instance().setMcpConnected(false);
    AiPanel::instance().setClientName({});
}

void McpServer::poll() {
    if (!listening_.load()) return;
    if (transport_.load() == Transport::Stdio) {
        std::vector<std::string> batch;
        {
            std::lock_guard<std::mutex> lock(ioMu_);
            batch.swap(stdioQueue_);
        }
        for (const auto& line : batch) handleMessage(line);
        if (stdinClosed_.load() && batch.empty() && initialized_) {
            resetMcpFrameCapture();
            initialized_ = false;
        }
        flushPendingSceneDirectorInstall(Debugger::instance().vm());
        pollMcpInputBatches();
        runPendingVisionDump();
        return;
    }
    acceptNonBlocking();
    readAndDispatch();
    flushPendingSceneDirectorInstall(Debugger::instance().vm());
    pollMcpInputBatches();
    runPendingVisionDump();
}

void McpServer::acceptNonBlocking() {
    if (!server_ || client_) return;
    try {
        Poco::Net::SocketAddress clientAddr;
        Poco::Net::StreamSocket  ss = server_->acceptConnection(clientAddr);
        ss.setBlocking(true);
        ss.setReceiveTimeout(Poco::Timespan(0, 1000));
        client_ = std::make_unique<Poco::Net::StreamSocket>(ss);
        hasClient_.store(true);
        resetMcpFrameCapture();
        recvBuf_.clear();
        initialized_ = false;
        AiPanel::instance().setMcpConnected(true);
        AiPanel::instance().addLog("system", "mcp.accept", "client connected");
    } catch (const Poco::TimeoutException&) {
    } catch (const Poco::Net::NetException&) {
    } catch (...) {
    }
}

bool McpServer::sendLine(const std::string& json) {
    if (transport_.load() == Transport::Stdio) {
        std::lock_guard<std::mutex> lock(ioMu_);
        if (!stdioOut_) return false;
        try {
            const std::string frame = json + "\n";
            (*stdioOut_) << frame;
            stdioOut_->flush();
            return true;
        } catch (...) {
            return false;
        }
    }
    if (!client_) return false;
    const std::string frame = json + "\n";
    try {
        const int sent = client_->sendBytes(frame.data(), static_cast<int>(frame.size()));
        return sent == static_cast<int>(frame.size());
    } catch (...) {
        client_.reset();
        hasClient_.store(false);
        AiPanel::instance().setMcpConnected(false);
        return false;
    }
}

void McpServer::readAndDispatch() {
    if (!client_) return;
    char buf[8192];
    try {
        const int n = client_->receiveBytes(buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0) {
                client_.reset();
                hasClient_.store(false);
                AiPanel::instance().setMcpConnected(false);
                AiPanel::instance().addLog("system", "mcp.disconnect", "client closed");
                resetMcpFrameCapture();
            }
            return;
        }
        recvBuf_.append(buf, static_cast<size_t>(n));
    } catch (const Poco::TimeoutException&) {
        return;
    } catch (const Poco::Net::NetException&) {
        client_.reset();
        hasClient_.store(false);
        AiPanel::instance().setMcpConnected(false);
        resetMcpFrameCapture();
        return;
    } catch (...) {
        client_.reset();
        hasClient_.store(false);
        AiPanel::instance().setMcpConnected(false);
        return;
    }

    while (true) {
        const auto nl = recvBuf_.find('\n');
        if (nl == std::string::npos) break;
        std::string line = recvBuf_.substr(0, nl);
        recvBuf_.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        handleMessage(line);
    }
}

void McpServer::handleMessage(const std::string& json) {
    try {
        Poco::JSON::Parser parser;
        auto               var = parser.parse(json);
        auto               obj = var.extract<Poco::JSON::Object::Ptr>();
        if (!obj) return;

        std::string method;
        if (obj->has("method")) {
            try {
                method = obj->get("method").convert<std::string>();
            } catch (const Poco::Exception& e) {
                sendLine(makeError("null", -32600, std::string("bad method: ") + e.displayText()));
                return;
            }
        }
        const bool         hasId = obj->has("id");
        Poco::Dynamic::Var idVar;
        if (hasId) idVar = obj->get("id");
        std::string idJson = "null";
        try {
            if (hasId) idJson = idToJson(idVar);
        } catch (const Poco::Exception& e) {
            sendLine(makeError("null", -32600, std::string("bad id: ") + e.displayText()));
            return;
        }

        if (!hasId) {
            if (method == "notifications/initialized" || method == "initialized") {
                initialized_ = true;
                AiPanel::instance().addLog("system", "mcp.initialized", "handshake complete");
            }
            return;
        }

        auto params = [&]() -> Poco::JSON::Object::Ptr {
            if (!obj->has("params")) return Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            try {
                return obj->getObject("params");
            } catch (...) {
                return Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            }
        };

        if (method == "initialize") {
            sendLine(handleInitialize(*this, idJson, params()));
            return;
        }
        if (method == "ping") {
            sendLine(makeResult(idJson, "{}"));
            return;
        }
        if (method == "tools/list") {
            sendLine(handleToolsList(idJson));
            return;
        }
        if (method == "tools/call") {
            sendLine(handleToolsCall(*this, idJson, params()));
            return;
        }
        if (method == "resources/list") {
            sendLine(handleResourcesList(idJson));
            return;
        }
        if (method == "resources/read") {
            sendLine(handleResourcesRead(idJson, params()));
            return;
        }
        if (method == "prompts/list") {
            sendLine(handlePromptsList(idJson));
            return;
        }
        if (method == "prompts/get") {
            sendLine(handlePromptsGet(idJson, params()));
            return;
        }

        sendLine(makeError(idJson, -32601, "Method not found: " + method));
    } catch (const Poco::Exception& e) {
        sendLine(makeError("null", -32700, std::string("Parse error: ") + e.displayText()));
    } catch (const std::exception& e) {
        sendLine(makeError("null", -32603, e.what()));
    } catch (...) {
        sendLine(makeError("null", -32603, "Internal error"));
    }
}

}  // namespace eve::dev
