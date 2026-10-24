#include "devtools/McpSceneDirector.hpp"

#include "common/ScriptError.h"
#include "devtools/McpSquirrelJson.hpp"
#include "scripts.h"

#include <squirrel.h>

#include <atomic>
#include <mutex>

namespace eve::dev {
namespace {

constexpr const SQChar* kMcpSnippetSource       = _SC("memory://mcp/snippet");
constexpr const SQChar* kMcpSceneDirectorSource = _SC("memory://mcp/scene-director");

std::mutex       g_installMu;
std::atomic_bool g_installPending{false};

std::string snippetErrorText(HSQUIRRELVM vm, bool compile) {
    eve::script::ScriptErrorContext ctx =
        compile ? eve::script::captureCompileError(vm) : eve::script::takeLastScriptError(vm);
    if (!ctx.empty()) return eve::script::formatScriptError(ctx);
    return compile ? "compile failed" : "runtime failed";
}

bool sceneDirectorPresent(HSQUIRRELVM vm) {
    const std::string check = "return (\"scene_director\" in getroottable()) ? (scene_director != null) : false;";
    const std::string out   = callSceneDirectorReturn(vm, check, nullptr);
    return !out.empty() && out.find("true") != std::string::npos;
}

bool installSceneDirectorNow(HSQUIRRELVM vm, std::string* err) {
    const char* kit = eve::scene_director_content;
    if (!kit || !*kit) {
        if (err) *err = "scene_director.nut not embedded (rebuild EVScripts)";
        return false;
    }
    return runMcpVmSnippet(vm, kit, err);
}

}  // namespace

bool runMcpVmSnippet(HSQUIRRELVM vm, const std::string& source, std::string* err) {
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

bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err) {
    return ensureSceneDirectorInstalled(vm, err, /*allowHeavyWork=*/true);
}

bool ensureSceneDirectorInstalled(HSQUIRRELVM vm, std::string* err, bool allowHeavyWork) {
    if (!vm) {
        if (err) *err = "no VM";
        return false;
    }
    if (sceneDirectorPresent(vm)) {
        g_installPending.store(false);
        return true;
    }
    if (!allowHeavyWork) {
        g_installPending.store(true);
        if (err) *err = "deferred-to-update";
        return false;
    }
    std::lock_guard lock(g_installMu);
    if (sceneDirectorPresent(vm)) {
        g_installPending.store(false);
        return true;
    }
    const bool ok = installSceneDirectorNow(vm, err);
    if (ok) g_installPending.store(false);
    return ok;
}

void flushPendingSceneDirectorInstall(HSQUIRRELVM vm) {
    if (!g_installPending.load() || !vm) return;
    std::string err;
    (void)ensureSceneDirectorInstalled(vm, &err, /*allowHeavyWork=*/true);
}

bool sceneDirectorInstallPending() { return g_installPending.load(); }

std::string sceneDirectorToolError(const std::string& name, const std::string& err) {
    if (err == "deferred-to-update") {
        return std::string("{\"ok\":false,\"retryable\":true,\"reason\":\"deferred-to-update\",\"tool\":\"") + name +
               "\",\"hint\":\"scene_director install was queued for the next eve.dev.poll / eve_update; retry this "
               "tool\"}";
    }
    return "error: " + name + ": " + (err.empty() ? "unknown" : err);
}

}  // namespace eve::dev
