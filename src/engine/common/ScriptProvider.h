#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ssq {
class Object;
}

namespace eve {

/**
 * @brief Generation-qualified identity of one published script provider.
 *
 * Stable data rather than a pointer: it survives the provider being retired, so a
 * stale handle is reported instead of resolving to whatever took its place.
 */
struct ScriptProviderHandle {
    std::string   name;
    std::uint64_t generation                                                           = 0;
    friend bool   operator==(const ScriptProviderHandle&, const ScriptProviderHandle&) = default;
};

/**
 * @brief One argument passed to a script provider method.
 *
 * The bridge marshals **values**: the four scalar kinds plus an owning object (`Table`) for
 * capabilities whose payload is a record rather than a number. A module that serves a
 * capability from script decides which of its fields cross the boundary, in what order.
 *
 * @remarks 用户 2026-09-20 的决定③（"全面加对象/表参数 + 结构化返回"）把这条边界从"只有标量"
 *          放宽到"标量 + 对象"。代价是这里多一层编组语义，所以 `call`/`callNumber` 的 `@cost` 要
 *          按"构造一个表"而不是"几个标量压栈"来读（见下）；`Table` 载荷仍走
 *          `common/SquirrelBinding.h` 那一套权威转换（递归/元素数上限、环检测、非有限数拒绝），
 *          桥本身不新增第二套递归转换。
 */
struct ScriptArg {
    /** @brief Active argument kind. */
    enum class Kind : std::uint8_t { Integer, Number, Boolean, Text, Table };

    Kind         kind    = Kind::Number;
    std::int64_t integer = 0;
    double       number  = 0.0;
    bool         boolean = false;
    std::string  text;
    /** @brief Object payload for `Kind::Table`; keys are the script side's field names. */
    Value::Object table;

    static ScriptArg fromInteger(std::int64_t value);
    static ScriptArg fromNumber(double value);
    static ScriptArg fromBoolean(bool value);
    static ScriptArg fromText(std::string value);
    /** @brief One object argument (a table on the script side). @cost One Value tree copy. */
    static ScriptArg fromTable(Value::Object fields);
};

class ScriptProviderRegistry;

/** @brief Read-only view of one published provider, for discovery and diagnostics. */
struct ScriptProviderInfo {
    std::string   name;
    std::uint64_t generation = 0;
    /** Capability it was published for; empty when published plainly. */
    std::string capability;
};

/**
 * @brief Binds one published provider to one native capability.
 *
 * The module that owns a capability supplies this: it knows its own interface and
 * therefore owns the marshalling. The registry only routes the binding, so no
 * central table has to know every capability.
 *
 * @return Success once the capability can be observed by its consumers.
 */
using ScriptCapabilityBinder = std::function<Result<void>(const ScriptProviderHandle&)>;

/** @brief Removes the binding a matching `ScriptCapabilityBinder` created. */
using ScriptCapabilityUnbinder = std::function<Result<void>(const ScriptProviderHandle&)>;

/**
 * @brief Named script objects that native systems drive through named methods.
 *
 * A native system needs to call into user script without knowing the script class.
 * This registry is that seam: a script object is published under a stable name, the
 * registry roots it for as long as it is published, and callers invoke a method by
 * name. A method the object does not implement is `Unsupported`, not a crash and not
 * a silent no-op.
 *
 * Deliberately minimal: it resolves nothing by itself. A caller that wants to serve
 * a `cap::` capability from script publishes the instance here and adapts it; the
 * registry only owns the identity, the rooting and the call.
 *
 * @ownership The registry owns one VM reference per published provider. Callers hold
 *            borrowed handles, never the object.
 * @lifetime A published provider stays valid until `unload` or `clearAll`. Handles
 *           outlive retirement on purpose and then report `StaleHandle`.
 * @thread Main/script thread only, like every other Squirrel entry point. There is no
 *         internal lock: a lookup and a call both touch the VM.
 * @reentrancy A published method is user script and may re-enter the registry
 *             (`publish`, `unload`) while it runs. `call` therefore re-resolves after
 *             the call only for its own bookkeeping and never holds a reference to
 *             the provider across an invocation it does not own.
 */
class EVENGINE_API ScriptProviderRegistry {
public:
    /** @brief Process-lifetime registry; `clearAll` is the only reset. */
    static ScriptProviderRegistry& instance();

    /**
     * @brief Publish one live script instance under a stable name.
     * @param name Non-empty stable provider name; republishing supersedes it.
     * @param instance Live script instance; the registry takes its own VM reference.
     * @return The new generation-qualified handle, or a structured failure.
     * @cost One VM reference plus a map insert. Call at load/publish time, never per
     *       frame.
     * @remarks Republishing the same name retires the previous generation without
     *          destroying it: outstanding handles report `StaleHandle` afterwards.
     *          When the superseded generation was capability-bound, its unbinder runs
     *          first and a failure there fails this call -- the superseded generation
     *          stays retired either way, so the name has no live provider afterwards.
     */
    [[nodiscard]] Result<ScriptProviderHandle> publish(std::string name, const ssq::Object& instance);

    /**
     * @brief Retire exactly one published generation.
     * @return Success; `StaleHandle` when it was already retired or superseded; or
     *         whatever a capability unbinder reported, after the generation is
     *         already retired.
     * @cost One map erase plus a VM release, plus one unbinder invocation when the
     *       provider was published through `publishForCapability`.
     */
    [[nodiscard]] Result<void> unload(const ScriptProviderHandle& handle);

    /** @brief Current published generation for `name`, or 0 when absent. */
    [[nodiscard]] std::uint64_t generationOf(std::string_view name) const;
    /** @brief Whether `name` currently has a published provider. */
    [[nodiscard]] bool contains(std::string_view name) const;
    /** @brief Number of published providers. */
    [[nodiscard]] std::size_t size() const;

    /**
     * @brief Names of every capability that has a declared script binding seam.
     * @return Names sorted lexicographically, so script-visible output is stable
     *         across runs and hash orders.
     * @cost O(capabilities) with one string copy each. Discovery only.
     */
    [[nodiscard]] std::vector<std::string> capabilities() const;

    /**
     * @brief Every currently published provider.
     * @return Entries sorted by name for deterministic script-visible output.
     * @cost O(published) with string copies. Discovery/diagnostics only.
     */
    [[nodiscard]] std::vector<ScriptProviderInfo> published() const;
    /**
     * @brief Drop every published provider and release its VM reference.
     * @remarks Test-only reset, and the hook the runtime uses at shutdown: a rooted
     *          `ssq::Object` must not outlive its VM.
     */
    void clearAll();

    /**
     * @brief Call `method()` on a published provider.
     * @return Success; `Unsupported` when the instance has no such method;
     *         `StaleHandle` when the generation is no longer published; `Failed`
     *         when the script call raised.
     * @cost One name lookup in the script object's delegate chain plus one script
     *       call. Callers on a per-entity path must resolve the handle once and keep
     *       it, not re-derive it per entity.
     */
    [[nodiscard]] Result<void> call(const ScriptProviderHandle& handle, std::string_view method) const;

    /**
     * @brief Call `method(dt)` on a published provider.
     * @param dt Seconds; the caller owns the simulation step, never a wall clock.
     * @return As the zero-argument overload.
     */
    [[nodiscard]] Result<void> call(const ScriptProviderHandle& handle, std::string_view method, double dt) const;

    /**
     * @brief Call `method(...)` with explicit arguments.
     * @param args Positional arguments in call order; may be empty.
     * @return As the zero-argument overload, plus `InvalidArgument` when the script
     *         method rejects an argument (wrong arity or type).
     * @cost One script call, plus one VM push per argument: a scalar argument is one push, a
     *       `Table` argument is one `eve::Value` tree projection (depth and element-count
     *       bounded by `common/SquirrelBinding.h`). Marshalling is the caller's: this is for
     *       capability seams that genuinely need several values or one record, not a
     *       general-purpose reflection bridge.
     * @remarks Overload resolution is unambiguous in practice because a braced
     *          argument list of one or more elements cannot initialize the `double`
     *          overload. Spell an intentionally empty list as the zero-argument
     *          overload instead of `{}`.
     */
    [[nodiscard]] Result<void> call(const ScriptProviderHandle& handle, std::string_view method,
                                    std::span<const ScriptArg> args) const;

    /**
     * @brief Call `method(dt)` and read a numeric result.
     * @param dt Seconds.
     * @return The script result as a double, or the same failures as `call`, plus
     *         `InvalidArgument` when the method returned a non-numeric value.
     */
    [[nodiscard]] Result<double> callNumber(const ScriptProviderHandle& handle, std::string_view method,
                                            double dt) const;

    /**
     * @brief Call `method(...)` with explicit scalar arguments and read a numeric result.
     * @param args Positional arguments in call order; may be empty.
     * @return As the `double` overload of `callNumber`.
     */
    [[nodiscard]] Result<double> callNumber(const ScriptProviderHandle& handle, std::string_view method,
                                            std::span<const ScriptArg> args) const;

    /**
     * @brief Call `method()` and read an array of numbers.
     * @return The script result as doubles, or the same failures as `call`, plus
     *         `InvalidArgument` when the method returned a non-array, a non-numeric
     *         element, or more than 64 elements.
     * @cost One script call plus one copy per element. For a target that reports a
     *       whole transform back, not for streaming data: the result is copied, not
     *       viewed.
     */
    [[nodiscard]] Result<std::vector<double>> callNumbers(const ScriptProviderHandle& handle,
                                                          std::string_view            method) const;

    /**
     * @brief Call `method(...)` with explicit scalar arguments and read an array of numbers.
     * @param args Positional arguments in call order; may be empty.
     * @return As the zero-argument overload of `callNumbers`.
     */
    [[nodiscard]] Result<std::vector<double>> callNumbers(const ScriptProviderHandle& handle, std::string_view method,
                                                          std::span<const ScriptArg> args) const;

    /**
     * @brief Call `method(...)` and read the script result as an owning `eve::Value`.
     *
     * 决定③（2026-09-20）的"结构化返回"这一半：插座可以拿到一个**对象/数组**而不是一个数。
     * 典型用途是 `ISensingCandidateProvider` / `IStateQuery` 这类"问一次、答一组"的接口。
     *
     * @param handle Published provider handle.
     * @param method Method name on the provider object.
     * @param args Positional arguments in call order; may be empty.
     * @return The result as an owning `Value` tree: `Null`/`Bool`/`Int64`/`Double`/`String`/
     *         `Array`/`Object`，或与 `call` 相同的失败（`Unsupported`/`StaleHandle`/`Failed`），
     *         外加转换失败时的 `InvalidArgument`。
     * @cost One script call plus one whole-tree projection. The result is **copied**, not viewed:
     *       per-frame callers should keep asking for scalars (`callNumber`) and only reach for a
     *       structured return when the payload genuinely is a record.
     * @remarks 载荷边界由 `common/SquirrelBinding.h` 的权威转换负责（默认深度 64、元素 100000，
     *          拒绝环与非有限数）。需要更紧的上限就用 `callNumbers`（64 个元素）那条路。
     * @lifetime 返回值拥有自己的存储，脚本 VM 销毁后依然可用。
     */
    [[nodiscard]] Result<Value> callValue(const ScriptProviderHandle& handle, std::string_view method,
                                          std::span<const ScriptArg> args) const;

    /** @brief No-argument overload of the structured call. @return As `callValue`. */
    [[nodiscard]] Result<Value> callValue(const ScriptProviderHandle& handle, std::string_view method) const;

    /**
     * @brief Declare that one native capability may be served by a script provider.
     *
     * Called once per capability by the module that owns the interface, at
     * composition time. The module supplies the marshalling because it is the only
     * party that knows its own interface; the registry just routes the binding, so
     * no central table has to enumerate every capability.
     *
     * @param capability Native `capabilityName` string, e.g. `"IVehicleDamageModifier"`.
     * @param binder Installs the native adapter for one published provider. Called
     *        synchronously during `publishForCapability`.
     * @param unbinder Removes the adapter again. Called by `unload`, `clearAll` and
     *        a failed `publishForCapability`.
     * @cost One map insert. Declare at composition time, never per frame.
     * @remarks Redeclaring the same capability replaces the previous pair. Both
     *          callbacks must be callable: a capability with no unbinder would leak
     *          its adapter across a hot reload.
     */
    void declareCapability(std::string capability, ScriptCapabilityBinder binder, ScriptCapabilityUnbinder unbinder);

    /** @brief Whether `capability` has a declared script binding seam. */
    [[nodiscard]] bool hasCapabilitySeam(std::string_view capability) const;

    /**
     * @brief Withdraw a declared seam when its owning module goes away.
     * @return Success, or `NotFound` when `capability` was never declared.
     * @cost One map erase.
     * @remarks Only the ability to bind again is withdrawn. A provider that is
     *          already published keeps its generation and its installed adapter, so
     *          unloading it still runs the unbinder it was published with. A seam
     *          that outlived its owner would otherwise advertise a binding that can
     *          only fail later, at publish time.
     */
    [[nodiscard]] Result<void> removeCapability(std::string_view capability);

    /**
     * @brief Publish one script instance as the provider of a declared capability.
     * @param capability Capability declared through `declareCapability`.
     * @param name Stable provider name; republishing supersedes it.
     * @param instance Live script instance; the registry takes its own VM reference.
     * @return The new handle, `NotFound` when nothing declared `capability`, or
     *         whatever the binder reported.
     * @cost One publish plus one binder invocation. Call at load/publish time.
     * @remarks Atomic: a binder failure unloads the fresh generation again, so a
     *          caller never observes a published provider that serves nothing.
     */
    [[nodiscard]] Result<ScriptProviderHandle> publishForCapability(std::string_view capability, std::string name,
                                                                    const ssq::Object& instance);

private:
    ScriptProviderRegistry();
    ~ScriptProviderRegistry();
    ScriptProviderRegistry(const ScriptProviderRegistry&)            = delete;
    ScriptProviderRegistry& operator=(const ScriptProviderRegistry&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    /**
     * @brief Resolve one handle to its live rooted instance.
     * @return A borrowed instance valid until the next `publish`/`unload`/`clearAll`
     *         on the same name, or `StaleHandle`/`Failed`.
     */
    [[nodiscard]] Result<const ssq::Object*> resolve(const ScriptProviderHandle& handle) const;
};

}  // namespace eve
