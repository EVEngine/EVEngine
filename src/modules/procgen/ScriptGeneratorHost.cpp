#include "procgen/ScriptGeneratorHost.h"

#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "procgen/Procgen.h"
#include "procgen/ProcgenScriptObjects.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace eve::procgen {
namespace {

template <class T>
eve::Result<T> hostFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.script-host"));
}

ssq::Table projectHostResult(HSQUIRRELVM vm, eve::Result<eve::Value>&& result) {
    const eve::Status status = result.status();
    if (!result.ok()) return eve::script::projectStatusResult(vm, status);
    eve::Value value = std::move(result).takeValue();
    return eve::script::projectStatusResult(vm, status, value);
}

class ActiveSystemScope {
public:
    ActiveSystemScope(std::unordered_set<std::string>& active, std::string system)
        : active_(active), system_(std::move(system)) {
        inserted_ = active_.insert(system_).second;
    }
    ~ActiveSystemScope() {
        if (inserted_) active_.erase(system_);
    }
    [[nodiscard]] bool inserted() const noexcept { return inserted_; }

private:
    std::unordered_set<std::string>& active_;
    std::string                      system_;
    bool                             inserted_ = false;
};

eve::Result<eve::Value> runGenerator(HSQUIRRELVM vm, Procgen& procgen, const ssq::Table& generator,
                                     const ssq::Object& params, const std::string& systemName,
                                     std::uint32_t seed) {
    if (systemName.empty())
        return hostFailure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                       "script generator system name must not be empty", "system");
    if (generator.isEmpty())
        return hostFailure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                       "script generator module must be a table", "generator");
    if (params.isEmpty())
        return hostFailure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                       "script generator Params must not be empty", "params");

    thread_local std::unordered_set<std::string> activeSystems;
    ActiveSystemScope                              active(activeSystems, systemName);
    if (!active.inserted())
        return hostFailure<eve::Value>(eve::DiagnosticCode::Conflict,
                                       "script generator cannot recursively rebuild the same system", "system");

    std::optional<ssq::Function> generate;
    try {
        generate.emplace(generator.findFunc("generate"));
    } catch (const std::exception& error) {
        return hostFailure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                       std::string("script generator requires generate(params, ctx): ") + error.what(),
                                       "generator.generate");
    }

    auto contextResult = Procgen::beginSystemHandle(systemName, seed);
    if (!contextResult.ok()) return eve::Result<eve::Value>::failure(contextResult.status());
    const ProcgenContextHandleRef contextRef = std::move(contextResult).takeValue();
    auto                           contextView = Procgen::resolve(contextRef);
    if (!contextView.isBound()) {
        Procgen::release(contextRef).ignore("release unresolved script generator context");
        return hostFailure<eve::Value>(eve::DiagnosticCode::StaleHandle,
                                       "script generator context became stale before execution", "context");
    }

    auto contextObject = eve::script::makeOwnedSquirrelInstance<ScriptProcgenContext>(
        vm, std::make_unique<ScriptProcgenContext>(contextRef));
    if (!contextObject.ok()) {
        const eve::Status status = contextObject.status();
        contextObject.ignore("script generator context proxy creation failed");
        procgen.abortSystem(contextRef).ignore("abort script generator after proxy creation failure");
        Procgen::release(contextRef).ignore("release script generator context after proxy creation failure");
        return eve::Result<eve::Value>::failure(status);
    }
    ssq::Object context = std::move(contextObject).takeValue();

    const SQInteger top = sq_gettop(vm);
    eve::script::clearLastScriptError(vm);
    sq_pushobject(vm, generate->getRaw());
    sq_pushobject(vm, generator.getRaw());
    sq_pushobject(vm, params.getRaw());
    sq_pushobject(vm, context.getRaw());
    const bool called = SQ_SUCCEEDED(sq_call(vm, 3, SQFalse, SQTrue));
    sq_settop(vm, top);

    if (!called) {
        std::string error = eve::script::formatLastScriptError(vm);
        if (error.empty()) error = "script generator callback failed";
        procgen.abortSystem(contextRef).ignore("abort failed script generator callback");
        Procgen::release(contextRef).ignore("release failed script generator context");
        return hostFailure<eve::Value>(eve::DiagnosticCode::Failed, std::move(error), "generator.generate");
    }

    auto committed = procgen.commitSystem(contextRef);
    const eve::Status commitStatus = committed.status();
    if (!committed.ok()) {
        Procgen::release(contextRef).ignore("release rejected script generator context");
        return eve::Result<eve::Value>::failure(commitStatus);
    }
    committed.ignore("script generator commit observed by host");

    eve::Value::Array outputs;
    const int         outputCount = procgen.getSystemOutputCount(systemName);
    outputs.reserve(static_cast<std::size_t>(outputCount));
    for (int index = 0; index < outputCount; ++index)
        outputs.emplace_back(procgen.getSystemOutputName(systemName, index));

    eve::Value receipt(eve::Value::Object{
        {"system", eve::Value(systemName)},
        {"seed", eve::Value(static_cast<std::int64_t>(procgen.getSystemSeed(systemName)))},
        {"revision", eve::Value(static_cast<std::int64_t>(procgen.getSystemRevision(systemName)))},
        {"outputs", eve::Value(std::move(outputs))},
    });
    Procgen::release(contextRef).ignore("release committed script generator context");
    return eve::Result<eve::Value>::success(std::move(receipt), commitStatus);
}

}  // namespace

void exposeScriptGeneratorHost(ssq::Class& cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("runScriptGenerator",
                [vm](Procgen* self, ssq::Table generator, ssq::Object params,
                     const std::string& systemName, std::uint32_t seed) {
                    if (!self)
                        return projectHostResult(
                            vm, hostFailure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                                        "runScriptGenerator requires a Procgen module", "procgen"));
                    return projectHostResult(vm, runGenerator(vm, *self, generator, params, systemName, seed));
                });
}

}  // namespace eve::procgen
