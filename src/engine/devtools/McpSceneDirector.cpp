#include "devtools/McpSceneDirector.hpp"

#include "common/ScriptError.h"
#include "devtools/McpDevBridge.hpp"
#include "devtools/McpSquirrelJson.hpp"
#include "scripts.h"

#include <squirrel.h>

namespace eve::dev {
namespace {

constexpr const SQChar* kMcpSnippetSource       = _SC("memory://mcp/snippet");
constexpr const SQChar* kMcpSceneDirectorSource = _SC("memory://mcp/scene-director");

std::string snippetErrorText(HSQUIRRELVM vm, bool compile, const SQChar* source) {
    eve::script::ScriptErrorContext ctx =
        compile ? eve::script::captureCompileError(vm) : eve::script::takeLastScriptError(vm);
    if (ctx.source.empty()) ctx.source = source;
    if (ctx.message.empty()) ctx.message = compile ? "compile failed" : "runtime failed";
    if (compile) ctx.stack.clear();
    std::string report = std::string("type: ") + (compile ? "compile_error" : "runtime_error") +
                         "\norigin: " + (source == kMcpSnippetSource ? "eve_run_script" : "scene_director") +
                         "\nsource: " + ctx.source + "\nline: " + std::to_string(ctx.line) +
                         "\ncolumn: " + std::to_string(ctx.column) +
                         "\nmessage: " + (compile ? ctx.message : eve::script::formatScriptError(ctx));
    mcpRecordSnippetError(report);
    return report;
}

}  // namespace

bool runMcpVmSnippet(HSQUIRRELVM vm, const std::string& source, std::string* err) {
    eve::script::clearLastScriptError(vm);
    const SQInteger top = sq_gettop(vm);
    // MCP snippets are transient input, not project scripts. Compile them
    // directly from memory so the unified project compiler cannot register a
    // file-shaped source that hot reload may later treat as user content.
    if (SQ_FAILED(
            sq_compilebuffer(vm, source.c_str(), static_cast<SQInteger>(source.size()), kMcpSnippetSource, SQTrue))) {
        sq_settop(vm, top);
        if (err) *err = snippetErrorText(vm, true, kMcpSnippetSource);
        return false;
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
        sq_settop(vm, top);
        if (err) *err = snippetErrorText(vm, false, kMcpSnippetSource);
        return false;
    }
    sq_settop(vm, top);
    return true;
}

std::string callSceneDirectorReturn(HSQUIRRELVM vm, const std::string& snippet, std::string* err) {
    eve::script::clearLastScriptError(vm);
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, snippet.c_str(), static_cast<SQInteger>(snippet.size()), kMcpSceneDirectorSource,
                                   SQTrue))) {
        sq_settop(vm, top);
        if (err) *err = snippetErrorText(vm, true, kMcpSceneDirectorSource);
        return {};
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
        sq_settop(vm, top);
        std::string text = snippetErrorText(vm, false, kMcpSceneDirectorSource);
        if (err)
            *err = text == "runtime failed" ? "runtime failed (is the scene_director kit installed?)" : std::move(text);
        return {};
    }
    std::string json = sqValueToJson(vm, -1);
    sq_settop(vm, top);
    return json;
}

bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err) {
    const std::string check = "return (\"scene_director\" in getroottable()) ? (scene_director != null) : false;";
    const std::string out   = callSceneDirectorReturn(vm, check, nullptr);
    if (!out.empty() && out.find("true") != std::string::npos) return true;
    const char* kit = eve::scene_director_content;
    if (!kit || !*kit) {
        if (err) *err = "scene_director.nut not embedded (rebuild EVScripts)";
        return false;
    }
    return runMcpVmSnippet(vm, kit, err);
}

std::string sceneDirectorToolError(const std::string& name, const std::string& err) {
    return "error: " + name + ": " + (err.empty() ? "unknown" : err);
}

}  // namespace eve::dev
