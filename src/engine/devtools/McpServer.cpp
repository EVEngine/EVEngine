#include "devtools/McpServer.hpp"

#include "common/EditorAutomation.h"
#include "common/ScriptError.h"
#include "devtools/Immortal.hpp"

#include "devtools/AiPanel.hpp"
#include "devtools/AgentDevelopmentMcp.hpp"
#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpDevBridge.hpp"
#include "devtools/RenderVision.hpp"
#include "devtools/SceneInspect.hpp"
#include "devtools/Snapshot.hpp"

#include "scripts.h"

#include "common/AudioQuery.h"
#include "common/Capability.h"
#include "common/EditorHost.h"
#include "common/Module.h"
#include "common/ParticlesQuery.h"
#include "common/PhysicsQuery.h"
#include "common/ProcgenQuery.h"
#include "common/RenderCapture.h"
#include "common/SceneQuery.h"
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

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <vector>

namespace eve::dev {
namespace {

constexpr const SQChar* kMcpSnippetSource       = _SC("memory://mcp/snippet");
constexpr const SQChar* kMcpSceneDirectorSource = _SC("memory://mcp/scene-director");

std::string mcpStringify(const Poco::Dynamic::Var& v) {
    std::ostringstream oss;
    // indent=0, step=0 => compact single-line JSON (required for newline framing)
    Poco::JSON::Stringifier::stringify(v, oss, 0, 0);
    return oss.str();
}

std::string mcpJsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

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

std::string textContentResult(const std::string& text, bool isError = false) {
    Poco::JSON::Object::Ptr result  = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr  content = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr item    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    item->set("type", "text");
    item->set("text", text);
    content->add(item);
    result->set("content", content);
    if (isError) result->set("isError", true);
    return mcpStringify(Poco::Dynamic::Var(result));
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
    return mcpStringify(Poco::Dynamic::Var(o));
}

Poco::Dynamic::Var argVar(Poco::JSON::Object::Ptr args, const char* key) {
    if (!args || !args->has(key)) return Poco::Dynamic::Var();
    try {
        return args->get(key);
    } catch (...) {
        return Poco::Dynamic::Var();
    }
}

std::string argString(Poco::JSON::Object::Ptr args, const char* key, const std::string& def = {}) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<std::string>();
    } catch (...) {
        return def;
    }
}

int argInt(Poco::JSON::Object::Ptr args, const char* key, int def = 0) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<int>();
    } catch (...) {
        return def;
    }
}

float argFloat(Poco::JSON::Object::Ptr args, const char* key, float def = 0.f) {
    if (!args || !args->has(key)) return def;
    try {
        return static_cast<float>(args->get(key).convert<double>());
    } catch (...) {
        return def;
    }
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

bool argBool(Poco::JSON::Object::Ptr args, const char* key, bool def = false) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<bool>();
    } catch (...) {
        return def;
    }
}

// ============================= Scene Director (AI scene-authoring) =============
// Thin C++ dispatchers over the Squirrel `scene_director` kit (src/scripts/
// scene_director.nut, embedded as eve::scene_director_content). Agents build /
// inspect scenes through `eve_scene_modify` / `eve_scene_info` /
// `eve_camera_generate` / `eve_scene_reset`; the kit owns the live Renderable3D /
// Camera3D / lighting state.

// Escape a string as a Squirrel single-line string literal (JSON escapes are
// a subset Squirrel understands: \n \r \t \\ \" and \uXXXX).
std::string sqStringLiteralEscape(const std::string& s) { return mcpJsonEscape(s); }

// Encode a Poco JSON value as a Squirrel literal (table/array/scalar/null).
std::string sqLiteralValue(const Poco::Dynamic::Var& v) {
    if (v.isEmpty()) return "null";
    if (v.isBoolean()) return v.convert<bool>() ? "true" : "false";
    if (v.isInteger()) return std::to_string(v.convert<Poco::Int64>());
    if (v.isNumeric()) {
        const double d = v.convert<double>();
        char         buf[64];
        std::snprintf(buf, sizeof(buf), "%g", d);
        return buf;
    }
    if (v.isString()) return std::string("\"") + sqStringLiteralEscape(v.convert<std::string>()) + "\"";
    if (v.isArray()) {
        std::string out = "[";
        try {
            auto arr = v.extract<Poco::JSON::Array::Ptr>();
            for (size_t i = 0; i < arr->size(); ++i) {
                if (i) out += ",";
                out += sqLiteralValue(arr->get(static_cast<unsigned int>(i)));
            }
        } catch (...) {
        }
        out += "]";
        return out;
    }
    if (v.isStruct()) {
        std::string out   = "{";
        bool        first = true;
        try {
            auto obj = v.extract<Poco::JSON::Object::Ptr>();
            for (const auto& kv : *obj) {
                if (!first) out += ",";
                first = false;
                out += "\"" + sqStringLiteralEscape(kv.first) + "\"=" + sqLiteralValue(kv.second);
            }
        } catch (...) {
        }
        out += "}";
        return out;
    }
    return "null";
}

std::string snippetErrorText(HSQUIRRELVM vm, bool compile) {
    eve::script::ScriptErrorContext ctx =
        compile ? eve::script::captureCompileError(vm) : eve::script::takeLastScriptError(vm);
    if (!ctx.empty()) return eve::script::formatScriptError(ctx);
    return compile ? "compile failed" : "runtime failed";
}

// Compile + run a snippet against the live VM (no return value captured).
bool runVmSnippet(HSQUIRRELVM vm, const std::string& source, std::string* err) {
    const SQInteger top = sq_gettop(vm);
    // MCP snippets are transient input, not project scripts. Compile them
    // directly from memory so the unified project compiler cannot register a
    // file-shaped source that hot reload may later treat as user content.
    if (SQ_FAILED(
            sq_compilebuffer(vm, source.c_str(), static_cast<SQInteger>(source.size()), kMcpSnippetSource, SQTrue))) {
        sq_settop(vm, top);
        if (err) *err = snippetErrorText(vm, true);
        return false;
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
        sq_settop(vm, top);
        if (err) *err = snippetErrorText(vm, false);
        return false;
    }
    sq_settop(vm, top);
    return true;
}

// Serialize a Squirrel value (at stack idx) to compact JSON.
std::string sqValueToJson(HSQUIRRELVM vm, SQInteger idx) {
    if (idx < 0) idx = sq_gettop(vm) + idx + 1;  // normalize relative -> absolute
    switch (sq_gettype(vm, idx)) {
        case OT_NULL: return "null";
        case OT_BOOL: {
            SQBool b = SQFalse;
            sq_getbool(vm, idx, &b);
            return b ? "true" : "false";
        }
        case OT_INTEGER: {
            SQInteger i = 0;
            sq_getinteger(vm, idx, &i);
            return std::to_string(i);
        }
        case OT_FLOAT: {
            SQFloat f = 0;
            sq_getfloat(vm, idx, &f);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(f));
            return buf;
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            return std::string("\"") + mcpJsonEscape(s ? s : "") + "\"";
        }
        case OT_ARRAY: {
            std::string out   = "[";
            bool        first = true;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, idx))) {
                if (!first) out += ",";
                first = false;
                out += sqValueToJson(vm, -1);
                sq_pop(vm, 2);
            }
            sq_pop(vm, 1);
            out += "]";
            return out;
        }
        case OT_TABLE: {
            std::string out   = "{";
            bool        first = true;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, idx))) {
                if (!first) out += ",";
                first = false;
                out += sqValueToJson(vm, -2);
                out += ":";
                out += sqValueToJson(vm, -1);
                sq_pop(vm, 2);
            }
            sq_pop(vm, 1);
            out += "}";
            return out;
        }
        default: return "\"<unserializable>\"";
    }
}

// Compile a snippet that returns a value, run it, and return the JSON of the
// return value (e.g. `return ::scene_director.info();`).
std::string callSceneDirectorReturn(HSQUIRRELVM vm, const std::string& snippet, std::string* err) {
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, snippet.c_str(), static_cast<SQInteger>(snippet.size()), kMcpSceneDirectorSource,
                                   SQTrue))) {
        sq_settop(vm, top);
        if (err) *err = snippetErrorText(vm, true);
        return {};
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
        sq_settop(vm, top);
        std::string text = snippetErrorText(vm, false);
        if (err)
            *err = text == "runtime failed" ? "runtime failed (is the scene_director kit installed?)" : std::move(text);
        return {};
    }
    std::string json = sqValueToJson(vm, -1);
    sq_settop(vm, top);
    return json;
}

// Install the scene_director kit into the live VM (idempotent).
bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err) {
    const std::string check = "return (\"scene_director\" in getroottable()) ? (scene_director != null) : false;";
    const std::string out   = callSceneDirectorReturn(vm, check, nullptr);
    if (!out.empty() && out.find("true") != std::string::npos) return true;
    const char* kit = eve::scene_director_content;
    if (!kit || !*kit) {
        if (err) *err = "scene_director.nut not embedded (rebuild EVScripts)";
        return false;
    }
    return runVmSnippet(vm, kit, err);
}

std::string sceneDirectorToolError(const std::string& name, const std::string& err) {
    return "error: " + name + ": " + (err.empty() ? "unknown" : err);
}

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

eve::IRenderCapture*    mcpCapture() { return eve::cap::query<eve::IRenderCapture>(); }
eve::ISceneQuery*       mcpScene() { return eve::cap::query<eve::ISceneQuery>(); }
eve::IPhysicsQuery*     mcpPhysics() { return eve::cap::query<eve::IPhysicsQuery>(); }
eve::IProcgenQuery*     mcpProcgen() { return eve::cap::query<eve::IProcgenQuery>(); }
eve::IParticlesQuery*   mcpParticles() { return eve::cap::query<eve::IParticlesQuery>(); }
eve::IAudioQuery*       mcpAudio() { return eve::cap::query<eve::IAudioQuery>(); }
eve::IEditorHost*       mcpHost() { return eve::cap::query<eve::IEditorHost>(); }
eve::IUIAutomation*     mcpUI() { return eve::cap::query<eve::IUIAutomation>(); }
eve::IEditorAutomation* mcpEditor() { return eve::cap::query<eve::IEditorAutomation>(); }

Poco::JSON::Object::Ptr renderableObservation(eve::IRenderCapture& capture, int entityId, int generation) {
    Poco::JSON::Object::Ptr output = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto inspected = capture.inspectRenderable3D(static_cast<std::uint32_t>(entityId),
                                                  static_cast<std::uint32_t>(generation));
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
    eve::SceneNodeInfo info;
    const bool found = host.empty() ? scene.getNode(node, &info) : scene.getNodeIn(host, node, &info);
    if (node.empty() || !found) {
        output->set("status", "not-found");
        output->set("error", "Scene node is missing from the observed host");
        return output;
    }
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
    const std::string observer = argString(args, "observer", "renderable3d");
    if (observer == "renderable3d") {
        auto* capture = mcpCapture();
        if (!capture) {
            if (error) *error = "graphics module not available";
            return nullptr;
        }
        const int entityId = argInt(args, "entityId", -1);
        const int generation = argInt(args, "generation", -1);
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
        return sceneNodeObservation(*scene, argString(args, "host"), argString(args, "node"));
    }
    if (error) *error = "unsupported observer: " + observer;
    return nullptr;
}

struct ExpectationMatch {
    bool valid = true;
    double maxError = 0.0;
    std::vector<std::string> mismatches;
    std::string error;
};

bool isJsonObject(const Poco::Dynamic::Var& value) {
    return value.type() == typeid(Poco::JSON::Object::Ptr);
}

bool isJsonArray(const Poco::Dynamic::Var& value) {
    return value.type() == typeid(Poco::JSON::Array::Ptr);
}

void compareExpectation(const Poco::Dynamic::Var& actual, const Poco::Dynamic::Var& expected,
                        const std::string& path, double tolerance, ExpectationMatch* match) {
    if (!match || !match->valid) return;
    if (isJsonObject(expected)) {
        if (!isJsonObject(actual)) {
            match->valid = false;
            match->error = "Expected object at " + path;
            return;
        }
        auto actualObject = actual.extract<Poco::JSON::Object::Ptr>();
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
        auto actualArray = actual.extract<Poco::JSON::Array::Ptr>();
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
        match->maxError = std::max(match->maxError, error);
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

Poco::JSON::Object::Ptr observationEvent(Poco::JSON::Object::Ptr descriptor,
                                         Poco::JSON::Object::Ptr observation,
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
    if (isAgentDevelopmentTool(name)) return callAgentDevelopmentTool(name, args);

    auto& dbg = Debugger::instance();
    auto& dap = DebugAdapter::instance();

    if (name == "eve_status") return engineStatusJson(mcp);

    // ============================= Game UI =============================
    if (name == "eve_ui_tree") {
        return mcpUI() ? mcpUI()->tree(argString(args, "host")) : "error: ui module not available";
    }
    if (name == "eve_ui_get") {
        return mcpUI() ? mcpUI()->get(argString(args, "host"), argString(args, "widget"))
                       : "error: ui module not available";
    }
    if (name == "eve_ui_click") {
        return mcpUI() ? mcpUI()->click(argString(args, "host"), argString(args, "widget"))
                       : "error: ui module not available";
    }

    // ============================= Editor command protocol =============================
    const auto editorInvoke = [&](const char* operation) {
        return mcpEditor() ? mcpEditor()->invoke(operation, mcpStringify(Poco::Dynamic::Var(args)))
                           : std::string("error: editor module not available");
    };
    if (name == "eve_editor_commands") return editorInvoke("commands");
    if (name == "eve_editor_target_create") return editorInvoke("target-create");
    if (name == "eve_editor_target_close") return editorInvoke("target-close");
    if (name == "eve_editor_inspect") return editorInvoke("inspect");
    if (name == "eve_editor_plan") return editorInvoke("plan");
    if (name == "eve_editor_commit") return editorInvoke("commit");
    if (name == "eve_editor_execute") return editorInvoke("execute");
    if (name == "eve_editor_observe_start") {
        auto* editor = mcpEditor();
        if (!editor) return "error: editor module not available";
        std::string error;
        Poco::JSON::Object::Ptr observation = runtimeObservation(args, &error);
        if (!observation) return "error: " + error;
        if (observation->getValue<std::string>("status") != "ok")
            return mcpStringify(Poco::Dynamic::Var(observation));
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
            Poco::Dynamic::Var described = describeParser.parse(editor->invoke(
                "observe-describe", mcpStringify(Poco::Dynamic::Var(args))));
            auto describedObject = described.extract<Poco::JSON::Object::Ptr>();
            if (!describedObject || describedObject->optValue<std::string>("status", "") != "applied")
                return mcpStringify(described);
            auto descriptor = describedObject->getObject("descriptor");
            std::string error;
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
            publish->set("sessionId", argString(args, "sessionId"));
            publish->set("event", event);
            Poco::JSON::Parser publishParser;
            Poco::Dynamic::Var published = publishParser.parse(editor->invoke(
                "observe-publish", mcpStringify(Poco::Dynamic::Var(publish))));
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
        std::string error;
        Poco::JSON::Object::Ptr before = runtimeObservation(args, &error);
        if (!before) return "error: " + error;
        if (before->getValue<std::string>("status") != "ok")
            return mcpStringify(Poco::Dynamic::Var(before));

        Poco::JSON::Object::Ptr output = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        output->set("before", before);
        const bool hasExpectation = args && args->has("expect");
        double tolerance = 0.001;
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
            Poco::Dynamic::Var transaction = parser.parse(editor->invoke("execute", mcpStringify(Poco::Dynamic::Var(args))));
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
            Poco::Dynamic::Var editorState = parser.parse(editor->invoke("inspect", mcpStringify(Poco::Dynamic::Var(args))));
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
        const int entityId = argInt(args, "entityId", -1);
        const int generation = argInt(args, "generation", -1);
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
        const int              limit = argInt(args, "limit", 500);
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
        const std::string  id = argString(args, "id");
        eve::SceneNodeInfo n;
        if (id.empty() || !scene->getNode(id, &n)) return "error: node not found: " + id;
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
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
        const std::string id = argString(args, "id");
        if (id.empty()) return "error: node not found: " + id;
        bool changed = false;
        if (args && args->has("x") && args->has("y") && args->has("z")) {
            changed = scene->setNodeTransform(id, argFloat(args, "x"), argFloat(args, "y"), argFloat(args, "z"));
        }
        if (args && args->has("visible")) {
            changed = scene->setNodeVisible(id, argBool(args, "visible")) || changed;
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
        const std::string algorithm = argString(args, "algorithm");
        if (algorithm.empty()) return "error: missing algorithm";
        std::vector<std::pair<std::string, std::string>> params;
        for (const auto& key : {"roomCount", "roomMin", "roomMax", "corridorWidth", "autotile", "scale", "octaves"}) {
            if (args && args->has(key)) params.emplace_back(key, std::to_string(argInt(args, key)));
        }
        if (args && args->has("corridorStyle")) params.emplace_back("corridorStyle", argString(args, "corridorStyle"));
        std::string err;
        std::string json = pg->generateMap(algorithm, argInt(args, "width", 32), argInt(args, "height", 32),
                                           static_cast<uint32_t>(argInt(args, "seed", 0)), params, &err);
        if (json.empty()) return "error: " + (err.empty() ? std::string("empty grid") : err);
        return json;
    }

    if (name == "eve_procgen_mesh") {
        auto* pg = mcpProcgen();
        if (!pg) return "error: Procgen module not available";
        const std::string recipe = argString(args, "recipe");
        if (recipe.empty()) return "error: missing recipe";
        std::string err;
        std::string json =
            pg->buildMesh(recipe, static_cast<uint32_t>(argInt(args, "seed", 0)), argInt(args, "width", -1),
                          argInt(args, "height", -1), argInt(args, "depth", -1), &err);
        if (json.empty()) return "error: " + (err.empty() ? std::string("build failed") : err);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
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
        const int id = ph->newWorld(argFloat(args, "gravityX", 0.f), argFloat(args, "gravityY", 900.f));
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
        if (!ph->rayCast(argInt(args, "world", 0), argFloat(args, "x1"), argFloat(args, "y1"), argFloat(args, "x2"),
                         argFloat(args, "y2"), &h))
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
        return ph->removeWorld(argInt(args, "world", -1)) ? "ok" : "error: unknown physics world id";
    }

    // ============================= Render =============================
    if (name == "eve_render_status") {
        return renderStatusText(mcpCapture());
    }

    if (name == "eve_render_describe") {
        const bool        fresh  = argBool(args, "fresh", false);
        const std::string reason = argString(args, "reason");
        return RenderVision::instance().describe(mcpCapture(), renderStatusText(mcpCapture()), fresh, reason);
    }

    if (name == "eve_render_vision_config") {
        auto& rv = RenderVision::instance();
        if (args && args->has("baseUrl")) rv.setBaseUrl(argString(args, "baseUrl"));
        if (args && args->has("apiKey")) rv.setApiKey(argString(args, "apiKey"));
        if (args && args->has("model")) rv.setModel(argString(args, "model"));
        if (args && args->has("path")) rv.setPath(argString(args, "path"));
        if (args && args->has("timeoutMs")) rv.setTimeoutMs(argInt(args, "timeoutMs", 20000));
        return rv.configJson();
    }

    if (name == "eve_screenshot") {
        auto* cap = mcpCapture();
        if (!cap) return "error: Graphics module not available";
        std::string path = argString(args, "path");
        if (path.empty()) path = "mcp_screenshot.png";
        int         w = 0, h = 0;
        std::string err;
        if (!cap->savePng(path, &w, &h, &err)) return "error: " + err;
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("path", path);
        o->set("width", w);
        o->set("height", h);
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
        if (!part->createEmitter(argInt(args, "buffer", 1000), argFloat(args, "x"), argFloat(args, "y"),
                                 argString(args, "preset"), argInt(args, "count", 100), &ex, &ey, &cnt))
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
        audio->setVolume(argFloat(args, "volume", 1.f));
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
        const std::string title = argString(args, "title", "EVEngine AI Host");
        return mcpHost() ? mcpHost()->openWindow(title, argInt(args, "width", 1280), argInt(args, "height", 800))
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
                    json = argString(args, "editor");
            } catch (...) {
                json = argString(args, "editor");
            }
        }
        if (json.empty()) return "error: missing editor";
        return mcpHost() ? mcpHost()->applyEditor(json) : "error: ui module not available";
    }
    if (name == "eve_host_editor_remove") {
        return mcpHost() ? mcpHost()->removeEditor(argString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_editor_list") {
        return mcpHost() ? mcpHost()->listEditors() : "error: ui module not available";
    }
    if (name == "eve_host_editor_state") {
        return mcpHost() ? mcpHost()->editorState(argString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_editor_set_value") {
        if (!args || !args->has("value")) return "error: missing value";
        return mcpHost() ? mcpHost()->setEditorValue(argString(args, "editor"), argString(args, "widget"),
                                                     mcpStringify(argVar(args, "value")))
                         : "error: ui module not available";
    }
    if (name == "eve_host_editor_save") {
        return mcpHost() ? mcpHost()->saveEditor(argString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_editor_unload") {
        return mcpHost() ? mcpHost()->unloadEditor(argString(args, "id")) : "error: ui module not available";
    }
    if (name == "eve_host_vm_register") {
        return mcpHost() ? mcpHost()->registerVM(argString(args, "name"), argString(args, "source"))
                         : "error: ui module not available";
    }
    if (name == "eve_host_vm_unregister") {
        return mcpHost() ? mcpHost()->unregisterVM(argString(args, "name")) : "error: ui module not available";
    }
    if (name == "eve_host_events") {
        return mcpHost() ? mcpHost()->consumeEvents(argString(args, "editor")) : "error: ui module not available";
    }
    if (name == "eve_host_widget_rect") {
        return mcpHost() ? mcpHost()->widgetRect(argString(args, "editor"), argString(args, "widget"))
                         : "error: ui module not available";
    }
    if (name == "eve_host_capture") {
        return mcpHost() ? mcpHost()->capture(argString(args, "path")) : "error: ui module not available";
    }
    if (name == "eve_host_script") {
        return mcpHost() ? mcpHost()->runScript(argString(args, "source")) : "error: ui module not available";
    }
    if (name == "eve_host_resource_reload") {
        return mcpHost() ? mcpHost()->reloadResource(argString(args, "path")) : "error: ui module not available";
    }
    if (name == "eve_host_hot_reload_status") {
        return mcpHost() ? mcpHost()->hotReloadStatus() : "error: ui module not available";
    }
    if (name == "eve_host_shutdown") {
        if (mcpHost()) mcpHost()->requestExit();
        return "ok";
    }

    if (name == "eve_eval") {
        const std::string expr = argString(args, "expression");
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
        const std::string actor = argString(args, "actor");
        const std::string bone  = argString(args, "bone");
        if (actor.empty()) return "error: missing actor";
        const std::string expr = "eve_mcp_skeleton_inspect(\"" + sqStringLiteralEscape(actor) +
                                 "\",\"" + sqStringLiteralEscape(bone) + "\")";
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
        const int              level = argInt(args, "level", 1);
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
        const std::string source = argString(args, "source");
        const int         line   = argInt(args, "line");
        if (source.empty() || line <= 0) return "error: need source and line";
        const int id = dbg.setBreakpoint(source, line, true);
        return "ok id=" + std::to_string(id);
    }
    if (name == "eve_clear_breakpoint") {
        const std::string source = argString(args, "source");
        const int         line   = argInt(args, "line");
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
            arr->add(o);
        }
        return mcpStringify(Poco::Dynamic::Var(arr));
    }

    if (name == "eve_watch_add") {
        const std::string expr = argString(args, "expression");
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
        const std::string json = argString(args, "json");
        std::string       err;
        if (!Snapshot::instance().restore(vm, json, &err)) return "error: " + err;
        return "ok";
    }
    if (name == "eve_snapshot_save") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string path = argString(args, "path");
        std::string       err;
        if (!Snapshot::instance().saveFile(vm, path, &err)) return "error: " + err;
        return "ok";
    }
    if (name == "eve_snapshot_load") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string path = argString(args, "path");
        std::string       err;
        if (!Snapshot::instance().loadFile(vm, path, &err)) return "error: " + err;
        return "ok";
    }

    if (name == "eve_error_slice") {
        const std::string& last = mcpLastReport();
        if (!last.empty()) return last;
        return mcpFormatError("no prior error; callgraph slice at latest site");
    }

    if (name == "eve_run_script") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        const std::string source = argString(args, "source");
        if (source.empty()) return "error: missing source";
        std::string error;
        if (!runVmSnippet(vm, source, &error)) return "error: " + error;
        return "ok";
    }

    if (name == "eve_ai_note") {
        const std::string text = argString(args, "text");
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
        if (!ensureSceneDirectorInstalled(vm, &err)) return sceneDirectorToolError(name, err);
        return "ok";
    }

    if (name == "eve_scene_director_status") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err)) return sceneDirectorToolError(name, err);
        return callSceneDirectorReturn(vm, "return ::scene_director.status();", &err);
    }

    if (name == "eve_scene_reset") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err)) return sceneDirectorToolError(name, err);
        const std::string out = callSceneDirectorReturn(vm, "return ::scene_director.reset();", &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    if (name == "eve_scene_modify") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err)) return sceneDirectorToolError(name, err);
        const std::string  action = argString(args, "action");
        const std::string  target = argString(args, "target");
        Poco::Dynamic::Var paramsVar;
        if (args && args->has("params")) {
            try {
                paramsVar = Poco::Dynamic::Var(args->getObject("params"));
            } catch (...) {
            }
        }
        const std::string snippet = "return ::scene_director.modify(" + sqLiteralValue(Poco::Dynamic::Var(action)) +
                                    "," + sqLiteralValue(Poco::Dynamic::Var(target)) + "," + sqLiteralValue(paramsVar) +
                                    ");";
        const std::string out     = callSceneDirectorReturn(vm, snippet, &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    if (name == "eve_camera_generate") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err)) return sceneDirectorToolError(name, err);
        const int         count   = argInt(args, "count", 6);
        const std::string snippet = "return ::scene_director.cameras(" + std::to_string(count) + ");";
        const std::string out     = callSceneDirectorReturn(vm, snippet, &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    if (name == "eve_scene_info") {
        HSQUIRRELVM vm = dbg.vm();
        if (!vm) return "error: no VM";
        std::string err;
        if (!ensureSceneDirectorInstalled(vm, &err)) return sceneDirectorToolError(name, err);
        const std::string out = callSceneDirectorReturn(vm, "return ::scene_director.info();", &err);
        if (!err.empty()) return sceneDirectorToolError(name, err);
        return out;
    }

    // ---- 场景巡检工具集（图像与 3D 几何数据严格同步） ----
    if (name == "inspect_generate_scene_camera_views") {
        const glm::vec3         center = argVec3(args, "center");
        const float             fov    = argFloat(args, "fov", 60.f);
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
        const float             fov = argFloat(args, "fov", 0.f);
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
        const std::string        dir = argString(args, "dir");
        const std::string        tag = argString(args, "tag", "frame");
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
            const float     fov = argFloat(args, "fov", 0.f);
            return SceneInspect::instance().visibleEntitiesJson(&eye, &tgt, fov);
        }
        const float fov = argFloat(args, "fov", 0.f);
        return SceneInspect::instance().visibleEntitiesJson(nullptr, nullptr, fov);
    }

    return "error: unknown tool " + name;
}

std::string handleInitialize(McpServer& mcp, const std::string& idJson, Poco::JSON::Object::Ptr params) {
    std::string clientName = "mcp-client";
    std::string protocol   = "2025-06-18";
    if (params) {
        try {
            if (params->has("clientInfo")) {
                auto info = params->getObject("clientInfo");
                if (info && info->has("name")) clientName = info->get("name").convert<std::string>();
            }
            if (params->has("protocolVersion")) protocol = params->get("protocolVersion").convert<std::string>();
        } catch (...) {
        }
    }
    (void)mcp;
    AiPanel::instance().setClientName(clientName);
    AiPanel::instance().addLog("system", "mcp.initialize", clientName);

    // Hand-built JSON keeps initialize compact (newline framing) without a Poco Object tree.
    const std::string resultJson =
        std::string("{\"protocolVersion\":\"") + mcpJsonEscape(protocol) +
        "\",\"capabilities\":{\"tools\":{},\"resources\":{},\"prompts\":{}},"
        "\"serverInfo\":{\"name\":\"evengine\",\"title\":\"EVEngine MCP\",\"version\":\"0.3.0\"},"
        "\"instructions\":\"EVEngine MCP for AI-assisted game development. eve_host_* tools create JSON-defined editor "
        "windows bound to Squirrel ViewModels (MVVM) for AI-crafted terrain/material/event editors.\"}";
    return makeResult(idJson, resultJson);
}

std::string handleToolsList(const std::string& idJson) {
    static const char* const kToolsParts[] = {
        "{\"tools\":["
        "{\"name\":\"eve_status\",\"description\":\"Runtime + debugger + MCP/DAP status JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_ui_tree\",\"description\":\"Inspect retained game UI hosts and semantic widget trees (not "
        "only eve_host editors).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"host\":{\"type\":\"string\",\"description\":\"optional "
        "exact host name\"}}}},"
        "{\"name\":\"eve_ui_get\",\"description\":\"Read one retained game UI widget by semantic id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"host\":{\"type\":\"string\"},\"widget\":{\"type\":"
        "\"string\"}},\"required\":[\"widget\"]}},"
        "{\"name\":\"eve_ui_click\",\"description\":\"Queue a semantic click by retained game UI widget id through the "
        "normal event path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"host\":{\"type\":\"string\"},\"widget\":{\"type\":"
        "\"string\"}},\"required\":[\"widget\"]}},"
        "{\"name\":\"eve_editor_commands\",\"description\":\"List editor commands allowed for automation in the active "
        "game/editor.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_editor_target_create\",\"description\":\"Create an Editor-owned document target or bind a live SceneHost/Renderable3D for Agent workflows.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"type\":{\"type\":\"string\",\"enum\":[\"scene\",\"scene-host\",\"material\",\"material-renderable3d\"]},\"host\":{\"type\":\"string\"},\"object\":{\"type\":\"string\"},\"entityId\":{\"type\":\"integer\",\"minimum\":0},\"generation\":{\"type\":\"integer\",\"minimum\":0}},\"required\":[\"target\",\"type\"]}},"
        "{\"name\":\"eve_editor_target_close\",\"description\":\"Unregister and destroy an Editor-owned automation target.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"}},\"required\":[\"target\"]}},"
        "{\"name\":\"eve_editor_inspect\",\"description\":\"Inspect authoritative structured state for an editor target.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"}},\"required\":[\"target\"]}},"
        "{\"name\":\"eve_editor_plan\",\"description\":\"Validate and retain a side-effect-free editor command plan.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"},\"payload\":{},"
        "\"expectedRevision\":{\"type\":\"integer\"}},\"required\":[\"command\"]}},"
        "{\"name\":\"eve_editor_commit\",\"description\":\"Commit a retained editor command plan.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"planId\":{\"type\":\"string\"}},\"required\":["
        "\"planId\"]}},"
        "{\"name\":\"eve_editor_execute\",\"description\":\"Execute an editor command immediately through the same "
        "transaction path as UI and scripts.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"},\"payload\":{}},"
        "\"required\":[\"command\"]}},"
        "{\"name\":\"eve_editor_execute_observe\",\"description\":\"Execute one Editor transaction and return correlated before/after runtime observations plus the Editor target snapshot. An optional expect object declares the desired observed subset; tolerance controls numeric matching and the response reports converged, maxError, and mismatch paths. The observer is renderable3d by default; scene-node observes a node in an optional named host. Missing or stale subjects and invalid expectations are rejected before mutation.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"command\":{\"type\":\"string\"},\"payload\":{},\"observer\":{\"type\":\"string\",\"enum\":[\"renderable3d\",\"scene-node\"]},\"entityId\":{\"type\":\"integer\",\"minimum\":0},\"generation\":{\"type\":\"integer\",\"minimum\":0},\"host\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"},\"expect\":{\"type\":\"object\"},\"tolerance\":{\"type\":\"number\",\"minimum\":0}},"
        "\"required\":[\"target\",\"command\"]}},"
        "{\"name\":\"eve_editor_observe_start\",\"description\":\"Start an RX-backed runtime observation session. The initial sample is validated immediately; consecutive duplicate samples are suppressed until the session is closed.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"observer\":{\"type\":\"string\",\"enum\":[\"renderable3d\",\"scene-node\"]},\"entityId\":{\"type\":\"integer\",\"minimum\":0},\"generation\":{\"type\":\"integer\",\"minimum\":0},\"host\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"},\"expect\":{\"type\":\"object\"},\"tolerance\":{\"type\":\"number\",\"minimum\":0}}}},"
        "{\"name\":\"eve_editor_observe_poll\",\"description\":\"Sample and drain changed events from an RX-backed observation session.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sessionId\":{\"type\":\"string\"}},\"required\":[\"sessionId\"]}},"
        "{\"name\":\"eve_editor_observe_close\",\"description\":\"Explicitly cancel and remove an RX-backed observation session.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"sessionId\":{\"type\":\"string\"}},\"required\":[\"sessionId\"]}},"
        "{\"name\":\"eve_editor_cancel\",\"description\":\"Discard a retained editor command plan without side "
        "effects.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"planId\":{\"type\":\"string\"}},\"required\":["
        "\"planId\"]}},"
        "{\"name\":\"eve_editor_undo\",\"description\":\"Undo the last editor transaction in the automation session.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_editor_redo\",\"description\":\"Redo the last editor transaction in the automation session.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_editor_diagnostics\",\"description\":\"Read structured diagnostics from the last editor "
        "automation operation.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_renderable3d_get\",\"description\":\"Read generation-qualified live Renderable3D transform and field-backed material state for Agent verification.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"entityId\":{\"type\":\"integer\",\"minimum\":0},\"generation\":{\"type\":\"integer\",\"minimum\":0}},\"required\":[\"entityId\",\"generation\"]}},"
        "{\"name\":\"eve_eval\",\"description\":\"Evaluate a Squirrel expression (local or roottable).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":["
        "\"expression\"]}},"
        "{\"name\":\"eve_skeleton_inspect\",\"description\":\"Inspect a published runtime skeleton hierarchy and bind/current bone transforms. The game provides eve_mcp_skeleton_inspect.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"actor\":{\"type\":\"string\"},\"bone\":{\"type\":\"string\"}},\"required\":[\"actor\"]}},"
        "{\"name\":\"eve_pause\",\"description\":\"Pause the game / script.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_continue\",\"description\":\"Continue from pause.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_over\",\"description\":\"Step over next statement.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_into\",\"description\":\"Step into next statement.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_out\",\"description\":\"Step out of current function.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_step_frame\",\"description\":\"Run one game frame then pause.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_stack\",\"description\":\"Return current script stack frames.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_locals\",\"description\":\"List locals at a stack level (default 1).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_set_breakpoint\",\"description\":\"Set a script breakpoint.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"line\":{\"type\":"
        "\"integer\"}},\"required\":[\"source\",\"line\"]}},"
        "{\"name\":\"eve_clear_breakpoint\",\"description\":\"Clear a script breakpoint.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"line\":{\"type\":"
        "\"integer\"}},\"required\":[\"source\",\"line\"]}},"
        "{\"name\":\"eve_list_breakpoints\",\"description\":\"List breakpoints.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_watch_add\",\"description\":\"Add a watch expression.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":["
        "\"expression\"]}},"
        "{\"name\":\"eve_watch_list\",\"description\":\"List watches with last values.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_snapshot_capture\",\"description\":\"Capture script-state snapshot JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_snapshot_restore\",\"description\":\"Restore script-state from snapshot JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"json\":{\"type\":\"string\"}},\"required\":[\"json\"]}}"
        ","
        "{\"name\":\"eve_snapshot_save\",\"description\":\"Save snapshot to a file path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}"
        ","
        "{\"name\":\"eve_snapshot_load\",\"description\":\"Load snapshot from a file path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}"
        ","
        "{\"name\":\"eve_error_slice\",\"description\":\"Return last error report / backward slice (script + "
        "render).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_run_script\",\"description\":\"Compile and run a short Squirrel snippet in the live VM.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"}},\"required\":["
        "\"source\"]}},"
        "{\"name\":\"eve_ai_note\",\"description\":\"Append a note to the DevTools AI session log.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}}"
        ","
        "{\"name\":\"eve_ai_log\",\"description\":\"Read the DevTools AI session log.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_scene_director_install\",\"description\":\"Install the scene-director authoring kit into the "
        "live VM (idempotent).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_scene_director_status\",\"description\":\"Scene-director kit status (installed / propCount / "
        "hasCamera).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_scene_reset\",\"description\":\"Clear all staged props, camera and reset lighting to "
        "defaults.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_scene_modify\",\"description\":\"Agent scene action: action in "
        "add_object|spawn|place|move_object|move|scale|rotate|rotation|remove_object|remove|visibility|material|"
        "lighting|set_lighting|camera|cameras|info|list|reset. spawn/move params: "
        "{id,kind,x,y,z,sx,sy,sz,yaw_deg,scale,pos,tint,seed,mesh_params,...}; lighting params: "
        "{timeOfDay,atmosphere,intensity,background}; camera params: {eye,target,fov}. Returns JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\"},\"target\":{\"type\":"
        "\"string\"},\"params\":{\"type\":\"object\"}},\"required\":[\"action\"]}},"
        "{\"name\":\"eve_camera_generate\",\"description\":\"Generate standardized QC camera rigs (eye/target/fov) "
        "orbiting the staged scene.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"count\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"eve_scene_info\",\"description\":\"Authoritative staged-scene truth JSON: props "
        "(id/kind/pos/scale/yaw_deg/tint) + count.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"inspect_generate_scene_camera_views\",\"description\":\"Generate a standard set of inspection "
        "camera views (road-level, bird's-eye, corner close-up, vista) around a center point.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"center\":{\"type\":\"array\",\"items\":{\"type\":"
        "\"number\"},\"description\":\"[x,y,z] center point to orbit (default "
        "[0,0,0])\"},\"fov\":{\"type\":\"number\",\"description\":\"base vertical FOV in degrees (default 60)\"}}}}"
        ","
        "{\"name\":\"set_camera_pose\",\"description\":\"Set the active camera pose from position + Euler rotation + "
        "optional FOV.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pos\":{\"type\":\"array\",\"items\":{\"type\":"
        "\"number\"},\"description\":\"[x,y,z] camera "
        "position\"},\"rot\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"[yawDeg,pitchDeg] "
        "facing\"},\"fov\":{\"type\":\"number\",\"description\":\"vertical FOV in degrees (0=keep "
        "current)\"}},\"required\":[\"pos\",\"rot\"]}}"
        ","
        "{\"name\":\"capture_render_frame\",\"description\":\"Atomically capture the current view and export matching "
        "buffers. PNG color frame + geometry JSON always; 'buffers' may add depth/normal (GBuffer), id (per-pixel "
        "render ID mask with JSON mapping) — shadow is unsupported on current backend.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\",\"description\":\"output "
        "directory (default: cache dir)\"},\"tag\":{\"type\":\"string\",\"description\":\"file name tag (default "
        "'frame')\"},\"buffers\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"optional: "
        "'color'|'depth'|'normal'|'id' (default ['color'])\"}}}}"
        ","
        "{\"name\":\"get_visible_entities_screen_bbox\",\"description\":\"Return visible scene entities in the frustum "
        "with their screen-space bbox, world AABB, id and asset label.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pos\":{\"type\":\"array\",\"items\":{\"type\":"
        "\"number\"},\"description\":\"optional [x,y,z] camera eye "
        "override\"},\"target\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"optional [x,y,z] "
        "look target override\"},\"fov\":{\"type\":\"number\",\"description\":\"optional FOV override (degrees)\"}}}}"
        ","
        "{\"name\":\"eve_scene_status\",\"description\":\"Active scene host name, node count and root id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_scene_nodes\",\"description\":\"List nodes of the active scene host (id/name/path/visible).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"limit\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"eve_scene_node_get\",\"description\":\"Read transform/visibility/parent/children of a scene node "
        "by id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}},"
        "{\"name\":\"eve_scene_node_set\",\"description\":\"Set position (x,y,z) or visibility of a scene node by "
        "id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},"
        "\"y\":{\"type\":\"number\"},\"z\":{\"type\":\"number\"},\"visible\":{\"type\":\"boolean\"}},\"required\":["
        "\"id\"]}},"
        "{\"name\":\"eve_procgen_recipes\",\"description\":\"List available procgen map algorithms and "
        "mesh/texture/PBR recipes.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_procgen_map\",\"description\":\"Generate a semantic tile grid with a procgen map algorithm.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"algorithm\":{\"type\":\"string\"},\"width\":{\"type\":"
        "\"integer\"},\"height\":{\"type\":\"integer\"},\"seed\":{\"type\":\"integer\"}},\"required\":[\"algorithm\"]}}"
        ","
        "{\"name\":\"eve_procgen_mesh\",\"description\":\"Build a procedural CPU mesh (mesh.* recipe) and return "
        "stats.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"recipe\":{\"type\":\"string\"},\"seed\":{\"type\":"
        "\"integer\"}},\"required\":[\"recipe\"]}},"
        "{\"name\":\"eve_physics_new_world\",\"description\":\"Create a 2D physics world and return its id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"gravityX\":{\"type\":\"number\"},\"gravityY\":{"
        "\"type\":\"number\"}}}},"
        "{\"name\":\"eve_physics_list_worlds\",\"description\":\"List live 2D physics worlds with gravity.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_physics_raycast\",\"description\":\"Raycast a segment in a physics world and report the "
        "closest hit.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"world\":{\"type\":\"integer\"},\"x1\":{\"type\":"
        "\"number\"},\"y1\":{\"type\":\"number\"},\"x2\":{\"type\":\"number\"},\"y2\":{\"type\":\"number\"}},"
        "\"required\":[\"world\",\"x1\",\"y1\",\"x2\",\"y2\"]}},"
        "{\"name\":\"eve_physics_remove_world\",\"description\":\"Destroy a 2D physics world by id.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"world\":{\"type\":\"integer\"}},\"required\":["
        "\"world\"]}},"
        "{\"name\":\"eve_render_status\",\"description\":\"Render window size, 3D frame flag, readback state and "
        "RenderFlow event count.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_screenshot\",\"description\":\"Capture the current frame to a PNG file (enables readback).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}},"
        "{\"name\":\"eve_render_describe\",\"description\":\"Capture the current frame and ask the configured vision "
        "model to describe it and relate it to render parameters. Cached unless fresh=true.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"fresh\":{\"type\":\"boolean\"},\"reason\":{\"type\":"
        "\"string\"}}}},"
        "{\"name\":\"eve_render_vision_config\",\"description\":\"Set/read the vision model config "
        "(baseUrl/apiKey/model/path/timeoutMs). No args returns current config (key masked).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"baseUrl\":{\"type\":\"string\"},\"apiKey\":{\"type\":"
        "\"string\"},\"model\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"timeoutMs\":{\"type\":"
        "\"integer\"}}}},"
        "{\"name\":\"eve_particles_status\",\"description\":\"Report live particle emitter count.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_particles_emit\",\"description\":\"Spawn a particle emitter at a position (optionally a "
        "preset) and emit particles.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},"
        "\"preset\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"eve_audio_status\",\"description\":\"Report master audio volume.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_audio_set_volume\",\"description\":\"Set master audio volume (0..1).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"volume\":{\"type\":\"number\"}},\"required\":["
        "\"volume\"]}},"
        "{\"name\":\"eve_audio_stop_all\",\"description\":\"Stop all playing audio sources.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "{\"name\":\"eve_host_status\",\"description\":\"Headless editor host status: window, editors, registered "
        "ViewModels, project root.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "{\"name\":\"eve_host_window_open\",\"description\":\"Create the host OS window (lazy; editors can also "
        "auto-open it).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"width\":{\"type\":"
        "\"integer\"},\"height\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"eve_host_window_close\",\"description\":\"Close the host OS window (MCP server stays alive).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "{\"name\":\"eve_host_window_state\",\"description\":\"Host window open state + size.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "{\"name\":\"eve_host_editor_apply\",\"description\":\"Apply an editor View (JSON widget tree). Auto-opens the "
        "host window on first use.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"object\"}},\"required\":["
        "\"editor\"]}},",
        "{\"name\":\"eve_host_editor_remove\",\"description\":\"Remove an editor panel from the session.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}},",
        "{\"name\":\"eve_host_editor_list\",\"description\":\"List editors (id/title/vm).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "{\"name\":\"eve_host_editor_state\",\"description\":\"Editor values + pending events (id omitted = all "
        "editors).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}}}},"
        "{\"name\":\"eve_host_editor_set_value\",\"description\":\"Set a widget value (JSON value). Writes the bound "
        "ViewModel and emits a change event.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"},\"widget\":{\"type\":"
        "\"string\"},\"value\":{}},\"required\":[\"editor\",\"widget\",\"value\"]}},",
        "{\"name\":\"eve_host_editor_save\",\"description\":\"Persist editor as editors/<id>.editor.json + <id>.vm.nut "
        "in the project.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}},",
        "{\"name\":\"eve_host_editor_unload\",\"description\":\"Remove an editor from the session (files stay on "
        "disk).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}},",
        "{\"name\":\"eve_host_vm_register\",\"description\":\"Compile a Squirrel ViewModel and register it by table "
        "name.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"source\":{\"type\":"
        "\"string\"}},\"required\":[\"name\",\"source\"]}},",
        "{\"name\":\"eve_host_vm_unregister\",\"description\":\"Unregister a ViewModel table.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}}"
        ",",
        "{\"name\":\"eve_host_events\",\"description\":\"Read and clear interaction events (human clicks/sliders or AI "
        "set_value).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"}}}},"
        "{\"name\":\"eve_host_widget_rect\",\"description\":\"Last-frame screen rect of a widget (for script drawing "
        "in viewports).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"editor\":{\"type\":\"string\"},\"widget\":{\"type\":"
        "\"string\"}},\"required\":[\"editor\",\"widget\"]}},",
        "{\"name\":\"eve_host_capture\",\"description\":\"Capture the host window to a PNG and return path/size.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}},"
        "{\"name\":\"eve_host_script\",\"description\":\"Run a Squirrel snippet in the host VM (full engine API + "
        "eve.host).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"}},\"required\":["
        "\"source\"]}},",
        "{\"name\":\"eve_host_resource_reload\",\"description\":\"Reload a project-scoped MCP host script or editor "
        "View/ViewModel resource without restarting.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}"
        ",",
        "{\"name\":\"eve_host_hot_reload_status\",\"description\":\"MCP host watcher count, reload counters and latest "
        "reload diagnostic.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "{\"name\":\"eve_host_shutdown\",\"description\":\"Exit the headless MCP host process.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},",
        "]}"};
    static const std::string kToolsJson = [] {
        std::string out;
        for (std::size_t index = 0; index < std::size(kToolsParts); ++index) {
            if (index + 1 == std::size(kToolsParts)) out += agentDevelopmentToolSchemas();
            out += kToolsParts[index];
        }
        return out;
    }();
    return makeResult(idJson, kToolsJson);
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
                      "{\"uri\":\"eve://error-report\",\"name\":\"error-report\",\"description\":\"Last DevTools error "
                      "slice report\",\"mimeType\":\"text/plain\"},"
                      "{\"uri\":\"eve://ai-session\",\"name\":\"ai-session\",\"description\":\"AI / MCP session "
                      "log\",\"mimeType\":\"text/plain\"},"
                      "{\"uri\":\"eve://callgraph\",\"name\":\"callgraph\",\"description\":\"CallGraph event "
                      "summary\",\"mimeType\":\"application/json\"}"
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
            "1) eve_pause then eve_snapshot_capture as baseline.\n"
            "2) Mutate or advance with eve_step_frame / eve_run_script.\n"
            "3) Assert with eve_eval / eve_watch_list.\n"
            "4) eve_snapshot_restore to reset; record notes via eve_ai_note.";
    } else if (name == "ai_game_review") {
        text =
            "Review this AI-generated or AI-assisted game build:\n"
            "1) eve_status + eve_ai_log for recent agent actions.\n"
            "2) Spot-check critical roots with eve_eval.\n"
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

}  // namespace

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
        return;
    }
    acceptNonBlocking();
    readAndDispatch();
    // Main-thread hook: run a pending breakpoint/error vision dump (if any) on
    // the render thread where Graphics readback is safe.
    if (RenderVision::instance().pending()) {
        auto* cap = mcpCapture();
        if (cap) RenderVision::instance().pollPending(cap, renderStatusText(cap));
    }
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
            }
            return;
        }
        recvBuf_.append(buf, static_cast<size_t>(n));
    } catch (const Poco::TimeoutException&) {
        return;
    } catch (const Poco::Net::NetException&) {
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
