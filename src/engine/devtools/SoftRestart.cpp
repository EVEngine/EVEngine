#include "devtools/SoftRestart.hpp"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Resource.h"
#include "common/ScriptError.h"
#include "devtools/McpSquirrelJson.hpp"
#include "devtools/ReloadSession.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <squirrel.h>

#include <string>

namespace eve::dev {
namespace {

constexpr int kMaxArgumentDepth = 16;

void pushJsonValue(HSQUIRRELVM vm, const Poco::Dynamic::Var& value, int depth) {
    if (depth > kMaxArgumentDepth || value.isEmpty()) {
        sq_pushnull(vm);
        return;
    }
    if (value.isBoolean()) {
        sq_pushbool(vm, value.convert<bool>() ? SQTrue : SQFalse);
        return;
    }
    if (value.isInteger()) {
        sq_pushinteger(vm, static_cast<SQInteger>(value.convert<Poco::Int64>()));
        return;
    }
    if (value.isNumeric()) {
        sq_pushfloat(vm, static_cast<SQFloat>(value.convert<double>()));
        return;
    }
    if (value.isString()) {
        const std::string text = value.convert<std::string>();
        sq_pushstring(vm, text.c_str(), static_cast<SQInteger>(text.size()));
        return;
    }
    if (value.type() == typeid(Poco::JSON::Array::Ptr)) {
        auto array = value.extract<Poco::JSON::Array::Ptr>();
        sq_newarray(vm, 0);
        const SQInteger target = sq_gettop(vm);
        for (unsigned index = 0; index < array->size(); ++index) {
            pushJsonValue(vm, array->get(index), depth + 1);
            sq_arrayappend(vm, target);
        }
        return;
    }
    if (value.type() == typeid(Poco::JSON::Object::Ptr)) {
        auto object = value.extract<Poco::JSON::Object::Ptr>();
        sq_newtable(vm);
        const SQInteger target = sq_gettop(vm);
        for (const auto& entry : *object) {
            sq_pushstring(vm, entry.first.c_str(), static_cast<SQInteger>(entry.first.size()));
            pushJsonValue(vm, entry.second, depth + 1);
            sq_newslot(vm, target, SQFalse);
        }
        return;
    }
    sq_pushnull(vm);
}

std::string lastVmError(HSQUIRRELVM vm) {
    const eve::script::ScriptErrorContext ctx = eve::script::takeLastScriptError(vm);
    if (!ctx.empty()) return eve::script::formatScriptError(ctx);
    return "soft restart failed";
}

/** Install `eve.restartArgs` (empty table when args is null). */
Result<void> installRestartArgs(HSQUIRRELVM vm, Poco::JSON::Object::Ptr args) {
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, _SC("eve"), -1);
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_TABLE) {
        sq_settop(vm, top);
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "eve table missing; is load.nut running?", "eve"));
    }
    sq_pushstring(vm, _SC("restartArgs"), -1);
    if (args)
        pushJsonValue(vm, Poco::Dynamic::Var(args), 0);
    else
        sq_newtable(vm);
    if (SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
        sq_settop(vm, top);
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "failed to set eve.restartArgs", "eve.restartArgs"));
    }
    sq_settop(vm, top);
    return Result<void>::success();
}

/** Call root `soft_restart_game(reloadScripts)` and require a boolean true. */
Result<bool> callSoftRestartGame(HSQUIRRELVM vm, bool reloadScripts) {
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, _SC("soft_restart_game"), -1);
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_CLOSURE) {
        sq_settop(vm, top);
        return Result<bool>::failure(Diagnostic::error(
            DiagnosticCode::NotFound,
            "soft_restart_game is missing; start the game with load.nut (eve run --mcp-port ...)",
            "soft_restart_game"));
    }
    sq_pushroottable(vm);  // this
    sq_pushbool(vm, reloadScripts ? SQTrue : SQFalse);
    if (SQ_FAILED(sq_call(vm, 2, SQTrue, SQTrue))) {
        const std::string err = lastVmError(vm);
        sq_settop(vm, top);
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::Failed, err, "soft_restart_game"));
    }
    SQBool ok = SQFalse;
    if (sq_gettype(vm, -1) == OT_BOOL) sq_getbool(vm, -1, &ok);
    sq_settop(vm, top);
    if (!ok) {
        return Result<bool>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "soft_restart_game returned false (see console / eve_console_read)",
            "soft_restart_game"));
    }
    return Result<bool>::success(true);
}

}  // namespace

std::uint32_t resetNativeStateProviders() {
    std::uint32_t ok = 0;
    eve::cap::forEach<eve::caps::IStateProvider>([&](eve::caps::IStateProvider* provider) {
        if (provider && provider->resetToDefaults()) ++ok;
    });
    return ok;
}

std::size_t resourceCacheCount() { return ResourceManager::getInstance().count(); }

Result<SoftRestartReport> executeSoftRestart(HSQUIRRELVM vm, SoftRestartRequest request) {
    if (!vm) {
        return Result<SoftRestartReport>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no VM attached", "vm"));
    }
    if (ReloadSession::instance().active()) {
        return Result<SoftRestartReport>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "a state hot-reload session is already active", "ReloadSession"));
    }

    SoftRestartReport report;
    report.resourceCountBefore = resourceCacheCount();
    report.args                = request.args ? request.args : Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    report.scriptsReloaded     = request.reloadScripts;

    auto installed = installRestartArgs(vm, report.args);
    if (!installed) return Result<SoftRestartReport>::failure(installed.status());

    auto restarted = callSoftRestartGame(vm, request.reloadScripts);
    if (!restarted) return Result<SoftRestartReport>::failure(restarted.status());

    report.initCalled         = true;
    report.resourceCountAfter = resourceCacheCount();
    // Soft restart must not drop the unified resource cache. A drop here means
    // someone called clear()/unload during the restart path — treat as failure
    // so agents do not silently pay for a cold asset load on the next frame.
    if (report.resourceCountAfter < report.resourceCountBefore) {
        return Result<SoftRestartReport>::failure(Diagnostic::error(
            DiagnosticCode::Failed,
            "resource cache shrank during soft restart (expected resources to be kept)",
            "ResourceManager"));
    }
    return Result<SoftRestartReport>::success(std::move(report));
}

}  // namespace eve::dev
