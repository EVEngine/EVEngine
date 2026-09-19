#include "common/ScriptProvider.h"

#include "common/Diagnostic.h"
#include "common/ScriptError.h"
#include "common/SquirrelOwnership.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include <unordered_map>
#include <utility>

namespace eve {

struct ScriptProviderRegistry::Impl {
    struct Entry {
        /** Rooted while published; unbound after retirement. */
        script::OwnedSquirrelObject instance;
        /** 0 means "never published". Retired entries keep an advanced value so an
         *  old handle can never resolve against a later incarnation. */
        std::uint64_t generation = 0;
    };

    std::unordered_map<std::string, Entry> entries;
};

namespace {

Diagnostic providerDiagnostic(DiagnosticCode code, std::string message, const std::string& path) {
    return Diagnostic::error(code, std::move(message), path);
}

/**
 * @brief Text of the VM's last script error, for an actionable diagnostic.
 *
 * Provider methods are user script: a mistake in one must surface as a `Failed`
 * result naming the script error, never as a raise into a native frame. Every call
 * below therefore passes `raiseerror = SQFalse` and reads the error here instead.
 */
std::string lastScriptErrorText(HSQUIRRELVM vm) {
    const script::ScriptErrorContext context = script::takeLastScriptError(vm);
    if (context.empty()) return "unknown script error";
    return script::formatScriptError(context);
}

/**
 * @brief Push the value of `instance.method`, or report why it cannot be called.
 * @return Success with the closure on top of the stack, or a structured failure.
 * @remarks Every failure path restores the VM stack to `top`.
 */
Result<void> pushProviderMethod(HSQUIRRELVM vm, HSQOBJECT instance, const std::string& method) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance);
    sq_pushstring(vm, method.c_str(), -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return Result<void>::failure(
            providerDiagnostic(DiagnosticCode::Unsupported, "script provider has no method '" + method + "'", method));
    }
    const SQObjectType type = sq_gettype(vm, -1);
    if (type != OT_CLOSURE && type != OT_NATIVECLOSURE) {
        sq_settop(vm, top);
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::Unsupported, "script provider member '" + method + "' is not a method", method));
    }
    return Result<void>::success();
}

}  // namespace

ScriptProviderRegistry::ScriptProviderRegistry() : impl_(new Impl()) {}
ScriptProviderRegistry::~ScriptProviderRegistry() = default;

ScriptProviderRegistry& ScriptProviderRegistry::instance() {
    // Function-local so the registry exists before any static module registration
    // touches it. Runtime::shutdown clears it before the VM is destroyed.
    static ScriptProviderRegistry registry;
    return registry;
}

Result<ScriptProviderHandle> ScriptProviderRegistry::publish(std::string name, const ssq::Object& instance) {
    if (name.empty()) {
        return Result<ScriptProviderHandle>::failure(
            providerDiagnostic(DiagnosticCode::InvalidArgument, "script provider name must not be empty", {}));
    }
    if (instance.isEmpty() || instance.getRaw()._type != OT_INSTANCE) {
        return Result<ScriptProviderHandle>::failure(
            providerDiagnostic(DiagnosticCode::InvalidArgument, "script provider must be a live class instance", name));
    }

    Impl::Entry& entry = impl_->entries[name];
    // Assignment releases any previous generation's VM reference.
    entry.instance                 = script::OwnedSquirrelObject(instance);
    const std::uint64_t generation = ++entry.generation;
    return Result<ScriptProviderHandle>::success(ScriptProviderHandle{std::move(name), generation});
}

Result<void> ScriptProviderRegistry::unload(const ScriptProviderHandle& handle) {
    auto found = impl_->entries.find(handle.name);
    if (found == impl_->entries.end() || !found->second.instance.isBound() ||
        found->second.generation != handle.generation) {
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::StaleHandle, "script provider generation is no longer published", handle.name));
    }
    // Keep the slot so the retired generation can never be resolved again, and so a
    // later publish cannot hand an old handle a new instance.
    found->second.instance = script::OwnedSquirrelObject{};
    ++found->second.generation;
    return Result<void>::success();
}

std::uint64_t ScriptProviderRegistry::generationOf(std::string_view name) const {
    auto found = impl_->entries.find(std::string(name));
    if (found == impl_->entries.end() || !found->second.instance.isBound()) return 0;
    return found->second.generation;
}

bool ScriptProviderRegistry::contains(std::string_view name) const { return generationOf(name) != 0; }

std::size_t ScriptProviderRegistry::size() const {
    std::size_t count = 0;
    for (const auto& [key, entry] : impl_->entries) {
        (void)key;
        if (entry.instance.isBound()) ++count;
    }
    return count;
}

void ScriptProviderRegistry::clearAll() { impl_->entries.clear(); }

Result<void> ScriptProviderRegistry::call(const ScriptProviderHandle& handle, std::string_view method) const {
    auto found = impl_->entries.find(handle.name);
    if (found == impl_->entries.end() || !found->second.instance.isBound() ||
        found->second.generation != handle.generation) {
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::StaleHandle, "script provider generation is no longer published", handle.name));
    }

    const ssq::Object& instance = found->second.instance.get();
    HSQUIRRELVM        vm       = instance.getHandle();
    const std::string  methodName(method);
    if (!vm) {
        return Result<void>::failure(
            providerDiagnostic(DiagnosticCode::Failed, "script provider has no live VM", methodName));
    }

    const HSQOBJECT raw    = instance.getRaw();
    auto            pushed = pushProviderMethod(vm, raw, methodName);
    if (!pushed) return Result<void>::failure(pushed.status());

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);  // `this`
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQFalse))) {
        const std::string detail = lastScriptErrorText(vm);
        sq_settop(vm, top);
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::Failed, "script provider method '" + methodName + "' raised: " + detail, methodName));
    }
    sq_settop(vm, top);
    return Result<void>::success();
}

Result<void> ScriptProviderRegistry::call(const ScriptProviderHandle& handle, std::string_view method,
                                          double dt) const {
    auto found = impl_->entries.find(handle.name);
    if (found == impl_->entries.end() || !found->second.instance.isBound() ||
        found->second.generation != handle.generation) {
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::StaleHandle, "script provider generation is no longer published", handle.name));
    }

    const ssq::Object& instance = found->second.instance.get();
    HSQUIRRELVM        vm       = instance.getHandle();
    const std::string  methodName(method);
    if (!vm) {
        return Result<void>::failure(
            providerDiagnostic(DiagnosticCode::Failed, "script provider has no live VM", methodName));
    }

    const HSQOBJECT raw    = instance.getRaw();
    auto            pushed = pushProviderMethod(vm, raw, methodName);
    if (!pushed) return Result<void>::failure(pushed.status());

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);         // `this`
    sq_pushfloat(vm, SQFloat(dt));  // the injected step
    if (SQ_FAILED(sq_call(vm, 2, SQFalse, SQFalse))) {
        const std::string detail = lastScriptErrorText(vm);
        sq_settop(vm, top);
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::Failed, "script provider method '" + methodName + "' raised: " + detail, methodName));
    }
    sq_settop(vm, top);
    return Result<void>::success();
}

Result<double> ScriptProviderRegistry::callNumber(const ScriptProviderHandle& handle, std::string_view method,
                                                  double dt) const {
    auto found = impl_->entries.find(handle.name);
    if (found == impl_->entries.end() || !found->second.instance.isBound() ||
        found->second.generation != handle.generation) {
        return Result<double>::failure(providerDiagnostic(
            DiagnosticCode::StaleHandle, "script provider generation is no longer published", handle.name));
    }

    const ssq::Object& instance = found->second.instance.get();
    HSQUIRRELVM        vm       = instance.getHandle();
    const std::string  methodName(method);
    if (!vm) {
        return Result<double>::failure(
            providerDiagnostic(DiagnosticCode::Failed, "script provider has no live VM", methodName));
    }

    const HSQOBJECT raw    = instance.getRaw();
    auto            pushed = pushProviderMethod(vm, raw, methodName);
    if (!pushed) return Result<double>::failure(pushed.status());

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);         // `this`
    sq_pushfloat(vm, SQFloat(dt));  // the injected step
    if (SQ_FAILED(sq_call(vm, 2, SQTrue, SQFalse))) {
        const std::string detail = lastScriptErrorText(vm);
        sq_settop(vm, top);
        return Result<double>::failure(providerDiagnostic(
            DiagnosticCode::Failed, "script provider method '" + methodName + "' raised: " + detail, methodName));
    }

    const SQObjectType resultType = sq_gettype(vm, -1);
    if (resultType != OT_FLOAT && resultType != OT_INTEGER) {
        sq_settop(vm, top);
        return Result<double>::failure(
            providerDiagnostic(DiagnosticCode::InvalidArgument,
                               "script provider method '" + methodName + "' did not return a number", methodName));
    }
    double result = 0.0;
    if (resultType == OT_FLOAT) {
        SQFloat value = 0.f;
        if (SQ_FAILED(sq_getfloat(vm, -1, &value))) {
            sq_settop(vm, top);
            return Result<double>::failure(providerDiagnostic(
                DiagnosticCode::InvalidArgument, "script provider returned an unreadable float", methodName));
        }
        result = double(value);
    } else {
        SQInteger value = 0;
        if (SQ_FAILED(sq_getinteger(vm, -1, &value))) {
            sq_settop(vm, top);
            return Result<double>::failure(providerDiagnostic(
                DiagnosticCode::InvalidArgument, "script provider returned an unreadable integer", methodName));
        }
        result = double(value);
    }
    sq_settop(vm, top);
    return Result<double>::success(result);
}

}  // namespace eve
