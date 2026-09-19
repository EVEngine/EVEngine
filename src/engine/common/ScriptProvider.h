#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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
     */
    [[nodiscard]] Result<ScriptProviderHandle> publish(std::string name, const ssq::Object& instance);

    /**
     * @brief Retire exactly one published generation.
     * @return Success, or `StaleHandle` when it was already retired or superseded.
     * @cost One map erase plus a VM release.
     */
    [[nodiscard]] Result<void> unload(const ScriptProviderHandle& handle);

    /** @brief Current published generation for `name`, or 0 when absent. */
    [[nodiscard]] std::uint64_t generationOf(std::string_view name) const;
    /** @brief Whether `name` currently has a published provider. */
    [[nodiscard]] bool contains(std::string_view name) const;
    /** @brief Number of published providers. */
    [[nodiscard]] std::size_t size() const;
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
     * @brief Call `method(dt)` and read a numeric result.
     * @param dt Seconds.
     * @return The script result as a double, or the same failures as `call`, plus
     *         `InvalidArgument` when the method returned a non-numeric value.
     */
    [[nodiscard]] Result<double> callNumber(const ScriptProviderHandle& handle, std::string_view method,
                                            double dt) const;

private:
    ScriptProviderRegistry();
    ~ScriptProviderRegistry();
    ScriptProviderRegistry(const ScriptProviderRegistry&)            = delete;
    ScriptProviderRegistry& operator=(const ScriptProviderRegistry&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve
