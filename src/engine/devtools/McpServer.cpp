#include "devtools/McpServer.hpp"

#include "devtools/AiPanel.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
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

std::string stringify(const Poco::Dynamic::Var& v) {
    std::ostringstream oss;
    // indent=0, step=0 => compact single-line JSON (required for newline framing)
    Poco::JSON::Stringifier::stringify(v, oss, 0, 0);
    return oss.str();
}

std::string jsonEscape(const std::string& s) {
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
        return std::string("\"") + jsonEscape(id.convert<std::string>()) + "\"";
    } catch (...) {
        return "null";
    }
}

std::string makeResult(const std::string& idJson, const std::string& resultJson) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson + ",\"result\":" + resultJson + "}";
}

std::string makeError(const std::string& idJson, int code, const std::string& message) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + idJson +
           ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" + jsonEscape(message) +
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
    return stringify(Poco::Dynamic::Var(result));
}

Poco::JSON::Object::Ptr toolDef(const std::string& name, const std::string& description,
                                Poco::JSON::Object::Ptr inputSchema) {
    Poco::JSON::Object::Ptr t = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    t->set("name", name);
    t->set("description", description);
    t->set("inputSchema", inputSchema);
    return t;
}

Poco::JSON::Object::Ptr emptyObjectSchema() {
    Poco::JSON::Object::Ptr schema = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    schema->set("type", "object");
    schema->set("properties", Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    return schema;
}

Poco::JSON::Object::Ptr stringPropSchema(const std::string& prop, const std::string& desc) {
    Poco::JSON::Object::Ptr schema = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    schema->set("type", "object");
    Poco::JSON::Object::Ptr props = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Object::Ptr p     = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    p->set("type", "string");
    p->set("description", desc);
    props->set(prop, p);
    schema->set("properties", props);
    Poco::JSON::Array::Ptr req = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    req->add(prop);
    schema->set("required", req);
    return schema;
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
    auto& dt  = DevTool::instance();
    Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    o->set("attached", dt.isAttached());
    o->set("paused", dbg.isPaused());
    o->set("pauseReason", pauseReasonName(dbg.lastPauseReason()));
    const auto& loc = dbg.pauseLocation();
    o->set("source", loc.source);
    o->set("line", loc.line);
    o->set("function", loc.function);
    o->set("mcpPort", mcp.port());
    o->set("mcpConnected", mcp.hasClient());
    o->set("dapPort", dt.dap().port());
    o->set("gameRoot", mcp.gameRoot());
    o->set("callgraphEvents", static_cast<int>(dt.graph().events().size()));
    o->set("ai", AiPanel::instance().statusLine());
    return stringify(Poco::Dynamic::Var(o));
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
    auto& dt  = DevTool::instance();

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
        return stringify(Poco::Dynamic::Var(o));
    }

    if (name == "eve_pause") {
        dbg.pause(PauseReason::PauseKey);
        dt.dap().notifyStopped(PauseReason::PauseKey, dbg.pauseLocation());
        return "paused";
    }
    if (name == "eve_continue") {
        dbg.resume();
        dt.dap().notifyContinued();
        return "continued";
    }
    if (name == "eve_step_over") {
        dbg.stepOver();
        dt.dap().notifyContinued();
        return "step_over";
    }
    if (name == "eve_step_into") {
        dbg.stepInto();
        dt.dap().notifyContinued();
        return "step_into";
    }
    if (name == "eve_step_out") {
        dbg.stepOut();
        dt.dap().notifyContinued();
        return "step_out";
    }
    if (name == "eve_step_frame") {
        dbg.stepFrame();
        dt.dap().notifyContinued();
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
        return stringify(Poco::Dynamic::Var(arr));
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
        return stringify(Poco::Dynamic::Var(arr));
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
        return stringify(Poco::Dynamic::Var(arr));
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
        return stringify(Poco::Dynamic::Var(arr));
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
        const std::string& last = dt.lastReport();
        if (!last.empty()) return last;
        return dt.formatError("no prior error; callgraph slice at latest site");
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

    Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
    result->set("protocolVersion", protocol);
    Poco::JSON::Object::Ptr caps = new Poco::JSON::Object();
    caps->set("tools", Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    caps->set("resources", Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    caps->set("prompts", Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    result->set("capabilities", caps);
    Poco::JSON::Object::Ptr serverInfo = new Poco::JSON::Object();
    serverInfo->set("name", std::string("evengine"));
    serverInfo->set("title", std::string("EVEngine MCP"));
    serverInfo->set("version", std::string("0.1.0"));
    result->set("serverInfo", serverInfo);
    result->set("instructions",
                std::string("EVEngine runtime MCP for AI-assisted game development and testing. "
                            "Use eve_status / eve_eval / eve_pause / eve_snapshot_* / "
                            "eve_error_slice to inspect and drive a live eve run --debug session."));
    return makeResult(idJson, stringify(Poco::Dynamic::Var(result)));
}

std::string handleToolsList(const std::string& idJson) {
    Poco::JSON::Array::Ptr tools = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    tools->add(toolDef("eve_status", "Runtime + debugger + MCP/DAP status JSON.",
                       emptyObjectSchema()));
    tools->add(toolDef("eve_eval", "Evaluate a Squirrel expression (local or roottable).",
                       stringPropSchema("expression", "Expression to evaluate")));
    tools->add(toolDef("eve_pause", "Pause the game / script.", emptyObjectSchema()));
    tools->add(toolDef("eve_continue", "Continue from pause.", emptyObjectSchema()));
    tools->add(toolDef("eve_step_over", "Step over next statement.", emptyObjectSchema()));
    tools->add(toolDef("eve_step_into", "Step into next statement.", emptyObjectSchema()));
    tools->add(toolDef("eve_step_out", "Step out of current function.", emptyObjectSchema()));
    tools->add(toolDef("eve_step_frame", "Run one game frame then pause.", emptyObjectSchema()));
    tools->add(toolDef("eve_stack", "Return current script stack frames.", emptyObjectSchema()));
    tools->add(toolDef("eve_locals", "List locals at a stack level (default 1).",
                       emptyObjectSchema()));
    {
        Poco::JSON::Object::Ptr schema = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        schema->set("type", "object");
        Poco::JSON::Object::Ptr props = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        Poco::JSON::Object::Ptr src   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        src->set("type", "string");
        props->set("source", src);
        Poco::JSON::Object::Ptr line = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        line->set("type", "integer");
        props->set("line", line);
        schema->set("properties", props);
        Poco::JSON::Array::Ptr req = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        req->add("source");
        req->add("line");
        schema->set("required", req);
        tools->add(toolDef("eve_set_breakpoint", "Set a script breakpoint.", schema));
        tools->add(toolDef("eve_clear_breakpoint", "Clear a script breakpoint.", schema));
    }
    tools->add(toolDef("eve_list_breakpoints", "List breakpoints.", emptyObjectSchema()));
    tools->add(toolDef("eve_watch_add", "Add a watch expression.",
                       stringPropSchema("expression", "Watch expression")));
    tools->add(toolDef("eve_watch_list", "List watches with last values.", emptyObjectSchema()));
    tools->add(toolDef("eve_snapshot_capture", "Capture script-state snapshot JSON.",
                       emptyObjectSchema()));
    tools->add(toolDef("eve_snapshot_restore", "Restore script-state from snapshot JSON.",
                       stringPropSchema("json", "Snapshot JSON")));
    tools->add(toolDef("eve_snapshot_save", "Save snapshot to a file path.",
                       stringPropSchema("path", "Output path")));
    tools->add(toolDef("eve_snapshot_load", "Load snapshot from a file path.",
                       stringPropSchema("path", "Input path")));
    tools->add(toolDef("eve_error_slice",
                       "Return last error report / backward slice (script + render).",
                       emptyObjectSchema()));
    tools->add(toolDef("eve_run_script",
                       "Compile and run a short Squirrel snippet in the live VM.",
                       stringPropSchema("source", "Squirrel source")));
    tools->add(toolDef("eve_ai_note", "Append a note to the DevTools AI session log.",
                       stringPropSchema("text", "Note text")));
    tools->add(toolDef("eve_ai_log", "Read the DevTools AI session log.", emptyObjectSchema()));

    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("tools", tools);
    return makeResult(idJson, stringify(Poco::Dynamic::Var(result)));
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
        if (args) detail = stringify(Poco::Dynamic::Var(args));
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
    Poco::JSON::Array::Ptr resources = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto add = [&](const char* uri, const char* name, const char* desc, const char* mime) {
        Poco::JSON::Object::Ptr r = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        r->set("uri", uri);
        r->set("name", name);
        r->set("description", desc);
        r->set("mimeType", mime);
        resources->add(r);
    };
    add("eve://status", "status", "Debugger / MCP / DAP status", "application/json");
    add("eve://error-report", "error-report", "Last DevTools error slice report", "text/plain");
    add("eve://ai-session", "ai-session", "AI / MCP session log", "text/plain");
    add("eve://callgraph", "callgraph", "CallGraph event summary", "application/json");

    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("resources", resources);
    return makeResult(idJson, stringify(Poco::Dynamic::Var(result)));
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
        text = DevTool::instance().lastReport();
        if (text.empty()) text = "(no error report yet)\n";
    } else if (uri == "eve://ai-session") {
        text = AiPanel::instance().formatLog(200);
        if (text.empty()) text = "(empty)\n";
    } else if (uri == "eve://callgraph") {
        auto& g = DevTool::instance().graph();
        Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        o->set("events", static_cast<int>(g.events().size()));
        o->set("stackDepth", static_cast<int>(g.currentStack().size()));
        text = stringify(Poco::Dynamic::Var(o));
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
    return makeResult(idJson, stringify(Poco::Dynamic::Var(result)));
}

std::string handlePromptsList(const std::string& idJson) {
    Poco::JSON::Array::Ptr prompts = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto add = [&](const char* name, const char* desc) {
        Poco::JSON::Object::Ptr p = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        p->set("name", name);
        p->set("description", desc);
        prompts->add(p);
    };
    add("debug_failure",
        "Investigate the latest script/render error using MCP tools and the error slice.");
    add("test_scenario",
        "Drive a reproducible in-engine test: pause, snapshot, eval assertions, continue.");
    add("ai_game_review",
        "Review live game state for AI-generated content issues.");

    Poco::JSON::Object::Ptr result = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    result->set("prompts", prompts);
    return makeResult(idJson, stringify(Poco::Dynamic::Var(result)));
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
    return makeResult(idJson, stringify(Poco::Dynamic::Var(result)));
}

}  // namespace

McpServer& McpServer::instance() {
    static McpServer inst;
    return inst;
}

McpServer::McpServer() = default;

McpServer::~McpServer() { stop(); }

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
