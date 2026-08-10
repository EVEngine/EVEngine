#include "devtools/McpServer.hpp"

#include "devtools/AiPanel.hpp"
#include "devtools/DebugAdapter.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/McpDevBridge.hpp"
#include "devtools/Snapshot.hpp"

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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace eve::dev {
namespace {

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
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
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
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson +
           ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" + mcpJsonEscape(message) +
           "\"}}";
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
        case PauseReason::Breakpoint:
            return "breakpoint";
        case PauseReason::Step:
            return "step";
        case PauseReason::Exception:
            return "exception";
        case PauseReason::Snapshot:
            return "snapshot";
        case PauseReason::PauseKey:
            return "pause";
        default:
            return "none";
    }
}

std::string engineStatusJson(const McpServer& mcp) {
    auto& dbg = Debugger::instance();
    Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    o->set("attached", mcpDevAttached());
    o->set("paused", dbg.isPaused());
    o->set("pauseReason", pauseReasonName(dbg.lastPauseReason()));
    const auto& loc = dbg.pauseLocation();
    o->set("source", loc.source);
    o->set("line", loc.line);
    o->set("function", loc.function);
    o->set("mcpPort", mcp.port());
    o->set("mcpConnected", mcp.hasClient());
    o->set("dapPort", DebugAdapter::instance().port());
    o->set("gameRoot", mcp.gameRoot());
    o->set("callgraphEvents", static_cast<int>(mcpCallgraphEvents()));
    o->set("ai", AiPanel::instance().statusLine());
    return mcpStringify(Poco::Dynamic::Var(o));
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

std::string callTool(McpServer& mcp, const std::string& name, Poco::JSON::Object::Ptr args) {
    auto& dbg = Debugger::instance();
    auto& dap = DebugAdapter::instance();

    if (name == "eve_status") return engineStatusJson(mcp);

    if (name == "eve_eval") {
        const std::string expr = argString(args, "expression");
        if (expr.empty()) return "error: missing expression";
        auto info = dbg.evaluate(expr);
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("expression", expr);
        o->set("name", info.name);
        o->set("value", info.value);
        o->set("type", info.type);
        o->set("ok", !info.value.empty() || info.type == "null" || !info.name.empty());
        return mcpStringify(Poco::Dynamic::Var(o));
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
        auto frames = dbg.stackTrace();
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
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
        const int level = argInt(args, "level", 1);
        auto      vars  = dbg.locals(level);
        Poco::JSON::Array::Ptr arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
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
        const SQInteger top = sq_gettop(vm);
        if (SQ_FAILED(sq_compilebuffer(vm, source.c_str(),
                                       static_cast<SQInteger>(source.size()),
                                       _SC("mcp_snippet.nut"), SQTrue))) {
            sq_settop(vm, top);
            return "error: compile failed";
        }
        sq_pushroottable(vm);
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return "error: runtime failed";
        }
        sq_settop(vm, top);
        return "ok";
    }

    if (name == "eve_ai_note") {
        const std::string text = argString(args, "text");
        if (text.empty()) return "error: missing text";
        AiPanel::instance().addNote(text);
        return "ok";
    }
    if (name == "eve_ai_log") return AiPanel::instance().formatLog(100);

    return "error: unknown tool " + name;
}

std::string handleInitialize(McpServer& mcp, const std::string& idJson,
                             Poco::JSON::Object::Ptr params) {
    std::string clientName = "mcp-client";
    std::string protocol   = "2025-06-18";
    if (params) {
        try {
            if (params->has("clientInfo")) {
                auto info = params->getObject("clientInfo");
                if (info && info->has("name"))
                    clientName = info->get("name").convert<std::string>();
            }
            if (params->has("protocolVersion"))
                protocol = params->get("protocolVersion").convert<std::string>();
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
        "\"serverInfo\":{\"name\":\"evengine\",\"title\":\"EVEngine MCP\",\"version\":\"0.1.0\"},"
        "\"instructions\":\"EVEngine runtime MCP for AI-assisted game development and testing.\"}";
    return makeResult(idJson, resultJson);
}

std::string handleToolsList(const std::string& idJson) {
    static const char* kToolsJson =
        "{\"tools\":["
        "{\"name\":\"eve_status\",\"description\":\"Runtime + debugger + MCP/DAP status JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_eval\",\"description\":\"Evaluate a Squirrel expression (local or roottable).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"]}},"
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
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}},\"required\":[\"source\",\"line\"]}},"
        "{\"name\":\"eve_clear_breakpoint\",\"description\":\"Clear a script breakpoint.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}},\"required\":[\"source\",\"line\"]}},"
        "{\"name\":\"eve_list_breakpoints\",\"description\":\"List breakpoints.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_watch_add\",\"description\":\"Add a watch expression.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"]}},"
        "{\"name\":\"eve_watch_list\",\"description\":\"List watches with last values.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_snapshot_capture\",\"description\":\"Capture script-state snapshot JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_snapshot_restore\",\"description\":\"Restore script-state from snapshot JSON.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"json\":{\"type\":\"string\"}},\"required\":[\"json\"]}},"
        "{\"name\":\"eve_snapshot_save\",\"description\":\"Save snapshot to a file path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
        "{\"name\":\"eve_snapshot_load\",\"description\":\"Load snapshot from a file path.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
        "{\"name\":\"eve_error_slice\",\"description\":\"Return last error report / backward slice (script + render).\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"eve_run_script\",\"description\":\"Compile and run a short Squirrel snippet in the live VM.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"}},\"required\":[\"source\"]}},"
        "{\"name\":\"eve_ai_note\",\"description\":\"Append a note to the DevTools AI session log.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}},"
        "{\"name\":\"eve_ai_log\",\"description\":\"Read the DevTools AI session log.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
        "]}";
    return makeResult(idJson, kToolsJson);
}

std::string handleToolsCall(McpServer& mcp, const std::string& idJson,
                            Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("name"))
        return makeError(idJson, -32602, "tools/call requires params.name");
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
        "{\"uri\":\"eve://status\",\"name\":\"status\",\"description\":\"Debugger / MCP / DAP status\",\"mimeType\":\"application/json\"},"
        "{\"uri\":\"eve://error-report\",\"name\":\"error-report\",\"description\":\"Last DevTools error slice report\",\"mimeType\":\"text/plain\"},"
        "{\"uri\":\"eve://ai-session\",\"name\":\"ai-session\",\"description\":\"AI / MCP session log\",\"mimeType\":\"text/plain\"},"
        "{\"uri\":\"eve://callgraph\",\"name\":\"callgraph\",\"description\":\"CallGraph event summary\",\"mimeType\":\"application/json\"}"
        "]}");
}

std::string handleResourcesRead(const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("uri"))
        return makeError(idJson, -32602, "resources/read requires params.uri");
    std::string uri;
    try {
        uri = params->get("uri").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "resources/read params.uri must be a string");
    }
    std::string       text;
    std::string       mime = "text/plain";
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
    return makeResult(idJson,
        "{\"prompts\":["
        "{\"name\":\"debug_failure\",\"description\":\"Investigate the latest script/render error using MCP tools and the error slice.\"},"
        "{\"name\":\"test_scenario\",\"description\":\"Drive a reproducible in-engine test: pause, snapshot, eval assertions, continue.\"},"
        "{\"name\":\"ai_game_review\",\"description\":\"Review live game state for AI-generated content issues.\"}"
        "]}");
}

std::string handlePromptsGet(const std::string& idJson, Poco::JSON::Object::Ptr params) {
    if (!params || !params->has("name"))
        return makeError(idJson, -32602, "prompts/get requires params.name");
    std::string name;
    try {
        name = params->get("name").convert<std::string>();
    } catch (...) {
        return makeError(idJson, -32602, "prompts/get params.name must be a string");
    }
    std::string       text;
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
    // Intentionally leaked so AiPanel/McpServer mutexes are never destroyed
    // while the other singleton's destructor still calls into them (macOS abort).
    static McpServer* inst = new McpServer();
    return *inst;
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
    listening_.store(false);
    hasClient_.store(false);
    port_.store(0);
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
        AiPanel::instance().addLog("system", "mcp.listen",
                                   "listening on 127.0.0.1:" + std::to_string(bound));
        return bound;
    } catch (...) {
        server_.reset();
        listening_.store(false);
        port_.store(0);
        AiPanel::instance().setMcpPort(0);
        return 0;
    }
}

void McpServer::stop() {
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
    recvBuf_.clear();
    listening_.store(false);
    hasClient_.store(false);
    port_.store(0);
    initialized_ = false;
    AiPanel::instance().setMcpPort(0);
    AiPanel::instance().setMcpConnected(false);
    AiPanel::instance().setClientName({});
}

void McpServer::poll() {
    if (!listening_.load()) return;
    acceptNonBlocking();
    readAndDispatch();
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
        const bool        hasId  = obj->has("id");
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
