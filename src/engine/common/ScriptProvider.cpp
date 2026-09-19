#include "common/ScriptProvider.h"

#include "common/Assert.h"
#include "common/Diagnostic.h"
#include "common/ScriptError.h"
#include "common/SquirrelOwnership.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve {

namespace {

/** One published provider: its rooted instance plus its publishing bookkeeping. */
struct ProviderEntry {
    /** Rooted while published; unbound after retirement. */
    script::OwnedSquirrelObject instance;
    /** 0 means "never published". Retired entries keep an advanced value so an old
     *  handle can never resolve against a later incarnation. */
    std::uint64_t generation = 0;
    /** Capability this provider was published for; empty when published plainly. */
    std::string capability;
    /** Removes the adapter `capability` was bound with. Empty when not bound. */
    ScriptCapabilityUnbinder unbinder;
};

/** Composition-time declaration that one capability may be served from script. */
struct CapabilitySeam {
    ScriptCapabilityBinder   binder;
    ScriptCapabilityUnbinder unbinder;
};

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
 * @brief Retire one entry: drop its VM reference and its capability adapter.
 *
 * `entry` is not touched after the unbinder runs, because the unbinder is
 * user-provided and may publish again, which can rehash the registry's map.
 *
 * @return The unbinder's outcome, or success when the entry had no adapter.
 */
Result<void> retireEntry(ProviderEntry& entry, const std::string& name) {
    const std::uint64_t retiredGeneration = entry.generation;
    entry.instance                        = script::OwnedSquirrelObject{};
    ++entry.generation;
    entry.capability.clear();
    ScriptCapabilityUnbinder unbinder = std::move(entry.unbinder);
    entry.unbinder                    = {};
    if (!unbinder) return Result<void>::success();
    const ScriptProviderHandle retired{name, retiredGeneration};
    return unbinder(retired);
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

/** @brief Push one marshalled argument; the inverse of the `ScriptArg` factories. */
void pushScriptArg(HSQUIRRELVM vm, const ScriptArg& arg) {
    switch (arg.kind) {
        case ScriptArg::Kind::Integer: sq_pushinteger(vm, SQInteger(arg.integer)); break;
        case ScriptArg::Kind::Number: sq_pushfloat(vm, SQFloat(arg.number)); break;
        case ScriptArg::Kind::Boolean: sq_pushbool(vm, arg.boolean ? SQTrue : SQFalse); break;
        case ScriptArg::Kind::Text: sq_pushstring(vm, arg.text.c_str(), SQInteger(arg.text.size())); break;
    }
}

/** @brief Read a numeric script value; the method name makes the failure actionable. */
Result<double> readNumericResult(HSQUIRRELVM vm, const std::string& methodName) {
    const SQObjectType resultType = sq_gettype(vm, -1);
    if (resultType != OT_FLOAT && resultType != OT_INTEGER) {
        return Result<double>::failure(
            providerDiagnostic(DiagnosticCode::InvalidArgument,
                               "script provider method '" + methodName + "' did not return a number", methodName));
    }
    if (resultType == OT_FLOAT) {
        SQFloat value = 0.f;
        if (SQ_FAILED(sq_getfloat(vm, -1, &value))) {
            return Result<double>::failure(providerDiagnostic(
                DiagnosticCode::InvalidArgument, "script provider returned an unreadable float", methodName));
        }
        return Result<double>::success(double(value));
    }
    SQInteger value = 0;
    if (SQ_FAILED(sq_getinteger(vm, -1, &value))) {
        return Result<double>::failure(providerDiagnostic(
            DiagnosticCode::InvalidArgument, "script provider returned an unreadable integer", methodName));
    }
    return Result<double>::success(double(value));
}

/** Upper bound on a returned array: the payload is untrusted script data. */
constexpr SQInteger kMaxResultElements = 64;

/** @brief Read the top of the stack as a number, whatever numeric kind it is. */
Result<double> readNumericElement(HSQUIRRELVM vm, SQInteger index, const std::string& methodName) {
    const SQObjectType type = sq_gettype(vm, index);
    if (type == OT_FLOAT) {
        SQFloat value = 0.f;
        if (SQ_FAILED(sq_getfloat(vm, index, &value))) {
            return Result<double>::failure(providerDiagnostic(
                DiagnosticCode::InvalidArgument, "script provider returned an unreadable float", methodName));
        }
        return Result<double>::success(double(value));
    }
    SQInteger value = 0;
    if (SQ_FAILED(sq_getinteger(vm, index, &value))) {
        return Result<double>::failure(providerDiagnostic(
            DiagnosticCode::InvalidArgument, "script provider returned an unreadable integer", methodName));
    }
    return Result<double>::success(double(value));
}

/** @brief Read the top of the stack as an array of numbers. */
Result<std::vector<double>> readNumericArray(HSQUIRRELVM vm, const std::string& methodName) {
    if (sq_gettype(vm, -1) != OT_ARRAY) {
        return Result<std::vector<double>>::failure(
            providerDiagnostic(DiagnosticCode::InvalidArgument,
                               "script provider method '" + methodName + "' did not return an array", methodName));
    }
    const SQInteger count = sq_getsize(vm, -1);
    if (count < 0 || count > kMaxResultElements) {
        return Result<std::vector<double>>::failure(
            providerDiagnostic(DiagnosticCode::InvalidArgument,
                               "script provider method '" + methodName + "' returned " + std::to_string(count) +
                                   " elements; the limit is " + std::to_string(kMaxResultElements),
                               methodName));
    }

    std::vector<double> out;
    out.reserve(std::size_t(count));
    const SQInteger top = sq_gettop(vm);
    for (SQInteger i = 0; i < count; ++i) {
        sq_pushinteger(vm, i);
        if (SQ_FAILED(sq_get(vm, -2))) break;
        auto element = readNumericElement(vm, -1, methodName);
        sq_settop(vm, top);  // drop the element again
        if (!element) return Result<std::vector<double>>::failure(element.status());
        out.push_back(element.value());
    }
    return Result<std::vector<double>>::success(std::move(out));
}

/** @brief Reader used when the caller does not want the script's return value. */
Result<double> ignoreResult(HSQUIRRELVM, const std::string&) { return Result<double>::success(0.0); }

/**
 * @brief Invoke one provider method with `args`, letting `readResult` collect the value.
 *
 * The whole call discipline lives here: a missing or non-method member is
 * `Unsupported` rather than a silent no-op, a raising method is `Failed` rather
 * than a raise into the native frame, and the VM stack is restored to its entry
 * depth on every path. The return value is always requested, so `readResult` only
 * has to decide whether it cares about it.
 *
 * @tparam Reader Callable taking `(HSQUIRRELVM, const std::string& methodName)`
 *         and returning `Result<T>`; it runs with the return value on top of the
 *         stack.
 */
template <class Reader>
auto invokeProvider(const ssq::Object& instance, std::string_view method, std::span<const ScriptArg> args,
                    Reader&& readResult) -> std::invoke_result_t<Reader, HSQUIRRELVM, const std::string&> {
    using Outcome = std::invoke_result_t<Reader, HSQUIRRELVM, const std::string&>;

    HSQUIRRELVM       vm = instance.getHandle();
    const std::string methodName(method);
    if (!vm) {
        return Outcome::failure(
            providerDiagnostic(DiagnosticCode::Failed, "script provider has no live VM", methodName));
    }

    const HSQOBJECT raw    = instance.getRaw();
    auto            pushed = pushProviderMethod(vm, raw, methodName);
    if (!pushed) return Outcome::failure(pushed.status());

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);  // `this`
    for (const ScriptArg& arg : args) pushScriptArg(vm, arg);

    const SQInteger argCount = SQInteger(args.size()) + 1;
    if (SQ_FAILED(sq_call(vm, argCount, SQTrue, SQFalse))) {
        const std::string detail = lastScriptErrorText(vm);
        sq_settop(vm, top);
        return Outcome::failure(providerDiagnostic(
            DiagnosticCode::Failed, "script provider method '" + methodName + "' raised: " + detail, methodName));
    }

    auto outcome = std::invoke(readResult, vm, methodName);
    sq_settop(vm, top);
    return outcome;
}

}  // namespace

struct ScriptProviderRegistry::Impl {
    std::unordered_map<std::string, ProviderEntry>  entries;
    std::unordered_map<std::string, CapabilitySeam> capabilities;
};

ScriptArg ScriptArg::fromInteger(std::int64_t value) {
    ScriptArg arg;
    arg.kind    = Kind::Integer;
    arg.integer = value;
    return arg;
}

ScriptArg ScriptArg::fromNumber(double value) {
    ScriptArg arg;
    arg.kind   = Kind::Number;
    arg.number = value;
    return arg;
}

ScriptArg ScriptArg::fromBoolean(bool value) {
    ScriptArg arg;
    arg.kind    = Kind::Boolean;
    arg.boolean = value;
    return arg;
}

ScriptArg ScriptArg::fromText(std::string value) {
    ScriptArg arg;
    arg.kind = Kind::Text;
    arg.text = std::move(value);
    return arg;
}

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

    // Superseding a capability-bound provider must also remove its adapter; leaving
    // it installed would keep routing native consumers into a dead script object.
    auto previous = impl_->entries.find(name);
    if (previous != impl_->entries.end() && previous->second.unbinder) {
        auto retired = retireEntry(previous->second, name);
        if (!retired) return Result<ScriptProviderHandle>::failure(retired.status());
    }

    // Re-lookup: running the unbinder above may have rehashed the map.
    ProviderEntry& entry = impl_->entries[name];
    entry.instance       = script::OwnedSquirrelObject(instance);
    entry.capability.clear();
    entry.unbinder                 = {};
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
    return retireEntry(found->second, handle.name);
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

std::vector<std::string> ScriptProviderRegistry::capabilities() const {
    std::vector<std::string> names;
    names.reserve(impl_->capabilities.size());
    for (const auto& [name, seam] : impl_->capabilities) {
        (void)seam;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<ScriptProviderInfo> ScriptProviderRegistry::published() const {
    std::vector<ScriptProviderInfo> entries;
    entries.reserve(impl_->entries.size());
    for (const auto& [name, entry] : impl_->entries) {
        if (!entry.instance.isBound()) continue;
        entries.push_back(ScriptProviderInfo{name, entry.generation, entry.capability});
    }
    std::sort(entries.begin(), entries.end(),
              [](const ScriptProviderInfo& lhs, const ScriptProviderInfo& rhs) { return lhs.name < rhs.name; });
    return entries;
}

void ScriptProviderRegistry::declareCapability(std::string capability, ScriptCapabilityBinder binder,
                                               ScriptCapabilityUnbinder unbinder) {
    const bool complete = !capability.empty() && binder && unbinder;
    EV_PARAM_CHECK(complete, "declareCapability requires a capability name, a binder and an unbinder");
    if (!complete) return;
    impl_->capabilities[std::move(capability)] = CapabilitySeam{std::move(binder), std::move(unbinder)};
}

bool ScriptProviderRegistry::hasCapabilitySeam(std::string_view capability) const {
    return impl_->capabilities.find(std::string(capability)) != impl_->capabilities.end();
}

Result<void> ScriptProviderRegistry::removeCapability(std::string_view capability) {
    const std::string key(capability);
    if (impl_->capabilities.erase(key) == 0) {
        return Result<void>::failure(providerDiagnostic(
            DiagnosticCode::NotFound, "capability '" + key + "' has no declared script binding seam", key));
    }
    return Result<void>::success();
}

Result<ScriptProviderHandle> ScriptProviderRegistry::publishForCapability(std::string_view capability, std::string name,
                                                                          const ssq::Object& instance) {
    auto seam = impl_->capabilities.find(std::string(capability));
    if (seam == impl_->capabilities.end()) {
        return Result<ScriptProviderHandle>::failure(providerDiagnostic(
            DiagnosticCode::NotFound,
            "no script binding seam is declared for capability '" + std::string(capability) + "'", name));
    }

    // Copy the seam before publishing: the binder is user code and may redeclare
    // capabilities, which can rehash the map and invalidate `seam`.
    ScriptCapabilityBinder   binder   = seam->second.binder;
    ScriptCapabilityUnbinder unbinder = seam->second.unbinder;

    auto published = publish(std::move(name), instance);
    if (!published) return Result<ScriptProviderHandle>::failure(published.status());
    const ScriptProviderHandle handle = published.value();

    // Record the seam on the entry *before* running the binder. A binder that fails
    // halfway has usually already installed part of its adapter, and the rollback
    // below removes it through this unbinder -- so the entry has to hold it by then
    // (`publish` clears both fields). Measured before the fix: the rollback ran with
    // an empty unbinder, so nothing was undone (plan doc §9.50).
    {
        auto pending = impl_->entries.find(handle.name);
        if (pending == impl_->entries.end() || pending->second.generation != handle.generation) {
            return Result<ScriptProviderHandle>::failure(providerDiagnostic(
                DiagnosticCode::Failed, "the published provider vanished before its binder ran", handle.name));
        }
        pending->second.capability = std::string(capability);
        pending->second.unbinder   = std::move(unbinder);
    }

    auto bound = binder(handle);
    if (!bound) {
        // Roll back so a caller never observes a published provider that serves
        // nothing. `unload` also runs the unbinder, which is what a partial bind
        // needs; its own outcome is secondary to the binder's.
        auto rolledBack = unload(handle);
        rolledBack.ignore("a failed bind is already reported; the rollback only has to retire the generation");
        return Result<ScriptProviderHandle>::failure(bound.status());
    }

    // Re-lookup after the binder: it is user code and may have re-published this same
    // name, which invalidates the iterator above.
    auto entry = impl_->entries.find(handle.name);
    if (entry == impl_->entries.end() || entry->second.generation != handle.generation) {
        // The binder re-published the same name; that generation owns its own bind.
        return Result<ScriptProviderHandle>::failure(providerDiagnostic(
            DiagnosticCode::Failed, "the capability binder replaced the provider it was binding", handle.name));
    }
    return Result<ScriptProviderHandle>::success(handle);
}

void ScriptProviderRegistry::clearAll() {
    // Detach every adapter before invoking any unbinder: an unbinder is
    // user-provided and may publish again, which would rehash the map under a live
    // iterator. Declared capability seams survive, because they are composition-time
    // declarations rather than published state.
    std::vector<std::pair<std::string, ScriptProviderHandle>> retired;
    std::vector<ScriptCapabilityUnbinder>                     unbinders;
    for (auto& [name, entry] : impl_->entries) {
        if (!entry.unbinder) continue;
        retired.emplace_back(name, ScriptProviderHandle{name, entry.generation});
        unbinders.push_back(std::move(entry.unbinder));
        entry.unbinder = {};
        entry.capability.clear();
    }
    impl_->entries.clear();

    for (std::size_t i = 0; i < unbinders.size(); ++i) {
        auto removed = unbinders[i](retired[i].second);
        // Shutdown teardown: the adapter dies with the VM either way, so the only
        // requirement is that the unbinder ran.
        removed.ignore("clearAll cannot report per-adapter teardown failures");
    }
}

Result<const ssq::Object*> ScriptProviderRegistry::resolve(const ScriptProviderHandle& handle) const {
    auto found = impl_->entries.find(handle.name);
    if (found == impl_->entries.end() || !found->second.instance.isBound() ||
        found->second.generation != handle.generation) {
        return Result<const ssq::Object*>::failure(providerDiagnostic(
            DiagnosticCode::StaleHandle, "script provider generation is no longer published", handle.name));
    }
    const ssq::Object& instance = found->second.instance.get();
    if (!instance.getHandle()) {
        return Result<const ssq::Object*>::failure(
            providerDiagnostic(DiagnosticCode::Failed, "script provider has no live VM", handle.name));
    }
    return Result<const ssq::Object*>::success(&instance);
}

Result<void> ScriptProviderRegistry::call(const ScriptProviderHandle& handle, std::string_view method) const {
    auto instance = resolve(handle);
    if (!instance) return Result<void>::failure(instance.status());
    auto invoked = invokeProvider(*instance.value(), method, {}, ignoreResult);
    if (!invoked) return Result<void>::failure(invoked.status());
    return Result<void>::success();
}

Result<void> ScriptProviderRegistry::call(const ScriptProviderHandle& handle, std::string_view method,
                                          double dt) const {
    const ScriptArg args[]   = {ScriptArg::fromNumber(dt)};
    auto            instance = resolve(handle);
    if (!instance) return Result<void>::failure(instance.status());
    auto invoked = invokeProvider(*instance.value(), method, args, ignoreResult);
    if (!invoked) return Result<void>::failure(invoked.status());
    return Result<void>::success();
}

Result<void> ScriptProviderRegistry::call(const ScriptProviderHandle& handle, std::string_view method,
                                          std::span<const ScriptArg> args) const {
    auto instance = resolve(handle);
    if (!instance) return Result<void>::failure(instance.status());
    auto invoked = invokeProvider(*instance.value(), method, args, ignoreResult);
    if (!invoked) return Result<void>::failure(invoked.status());
    return Result<void>::success();
}

Result<double> ScriptProviderRegistry::callNumber(const ScriptProviderHandle& handle, std::string_view method,
                                                  double dt) const {
    const ScriptArg args[]   = {ScriptArg::fromNumber(dt)};
    auto            instance = resolve(handle);
    if (!instance) return Result<double>::failure(instance.status());
    return invokeProvider(*instance.value(), method, args, readNumericResult);
}

Result<double> ScriptProviderRegistry::callNumber(const ScriptProviderHandle& handle, std::string_view method,
                                                  std::span<const ScriptArg> args) const {
    auto instance = resolve(handle);
    if (!instance) return Result<double>::failure(instance.status());
    return invokeProvider(*instance.value(), method, args, readNumericResult);
}

Result<std::vector<double>> ScriptProviderRegistry::callNumbers(const ScriptProviderHandle& handle,
                                                                std::string_view            method) const {
    auto instance = resolve(handle);
    if (!instance) return Result<std::vector<double>>::failure(instance.status());
    return invokeProvider(*instance.value(), method, {}, readNumericArray);
}

Result<std::vector<double>> ScriptProviderRegistry::callNumbers(const ScriptProviderHandle& handle,
                                                                std::string_view            method,
                                                                std::span<const ScriptArg>  args) const {
    auto instance = resolve(handle);
    if (!instance) return Result<std::vector<double>>::failure(instance.status());
    return invokeProvider(*instance.value(), method, args, readNumericArray);
}

}  // namespace eve
