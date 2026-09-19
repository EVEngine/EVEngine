#pragma once

// Capability registry: how one module uses another without depending on it.
//
// Modules normally reach each other by including the provider's header and
// calling it directly, which welds the two together at link time. That is fine
// downwards (map may depend on graphics) but not upwards: it is why filesystem
// ends up knowing about particles and why the SDL event pump knows about every
// input module. Those upward edges are what stop a module from being trimmed
// out of a build.
//
// A capability inverts that. The interface lives with the *consumer* (or in
// common/), the provider registers an implementation at startup, and the
// consumer queries it and copes with nullptr. Neither side includes the other.
// common/RenderTrace.h has worked this way for the render tracer; this
// generalises the same idea.
//
// Declaring an interface -- give it a stable name so the key survives the
// module being compiled into a plugin:
//
//     class IGpuInfo {
//     public:
//         static constexpr const char* capabilityName = "IGpuInfo";
//         virtual ~IGpuInfo() = default;
//         virtual std::string gpuName() const = 0;
//     };
//
// One provider (a service):
//
//     eve::cap::provide<IGpuInfo>(&myImpl);          // provider side
//     if (auto* g = eve::cap::query<IGpuInfo>())     // consumer side
//         name = g->gpuName();
//
// Many providers (handlers / listeners), dispatched in priority order,
// lower value first:
//
//     eve::cap::addListener<IHotReloadHandler>(&h, 10);
//     eve::cap::forEach<IHotReloadHandler>([&](IHotReloadHandler* h) { ... });
//
// Lifetime is the caller's: whatever is registered must outlive the registry
// entry, or be withdrawn with revoke() / removeListener() first. Registration
// is not thread-safe and is expected to happen during module construction;
// query() and forEach() are safe to call concurrently once registration has
// settled.

#include "common/Export.h"
#include "common/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::cap {

namespace detail {

EVENGINE_API void  provideRaw(const char* name, void* impl);
EVENGINE_API void* queryRaw(const char* name);
EVENGINE_API void  revokeRaw(const char* name, void* impl);

EVENGINE_API void   addListenerRaw(const char* name, void* impl, int priority);
EVENGINE_API void   removeListenerRaw(const char* name, void* impl);
EVENGINE_API size_t listenerCountRaw(const char* name);
EVENGINE_API void*  listenerAtRaw(const char* name, size_t index);

/** Drops every registration. Test-only; resets state between cases. */
EVENGINE_API void clearAllRaw();

}  // namespace detail

/** Register the single implementation of I, replacing any previous one. */
template <class I>
void provide(I* impl) {
    detail::provideRaw(I::capabilityName, impl);
}

/** The implementation of I, or nullptr when no module provides it. */
template <class I>
I* query() {
    return static_cast<I*>(detail::queryRaw(I::capabilityName));
}

/** Withdraw `impl`; a no-op when something else has since taken the slot. */
template <class I>
void revoke(I* impl) {
    detail::revokeRaw(I::capabilityName, impl);
}

/** Add one of possibly many implementations. Lower priority runs first. */
template <class I>
void addListener(I* impl, int priority = 0) {
    detail::addListenerRaw(I::capabilityName, impl, priority);
}

template <class I>
void removeListener(I* impl) {
    detail::removeListenerRaw(I::capabilityName, impl);
}

template <class I>
size_t listenerCount() {
    return detail::listenerCountRaw(I::capabilityName);
}

template <class I>
I* listenerAt(size_t index) {
    return static_cast<I*>(detail::listenerAtRaw(I::capabilityName, index));
}

/**
 * @brief Call `fn` for every implementation of I in priority order.
 *
 * Dispatch uses a snapshot of the listener pointers taken before the first
 * callback. Registrations and withdrawals made by a callback affect later
 * dispatches, but not the dispatch already in progress. A withdrawn listener
 * must therefore remain alive until the current dispatch returns.
 */
template <class I, class F>
void forEach(F&& fn) {
    const size_t n = detail::listenerCountRaw(I::capabilityName);
    std::vector<I*> snapshot;
    snapshot.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (auto* impl = static_cast<I*>(detail::listenerAtRaw(I::capabilityName, i)))
            snapshot.push_back(impl);
    }
    for (auto* impl : snapshot) fn(impl);
}

/**
 * @brief Call `fn` for each snapshotted implementation until one returns true.
 *
 * Listener mutations have the same next-dispatch semantics as forEach().
 * @return Whether any implementation returned true.
 */
template <class I, class F>
bool forEachUntil(F&& fn) {
    const size_t n = detail::listenerCountRaw(I::capabilityName);
    std::vector<I*> snapshot;
    snapshot.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (auto* impl = static_cast<I*>(detail::listenerAtRaw(I::capabilityName, i)))
            snapshot.push_back(impl);
    }
    for (auto* impl : snapshot)
        if (fn(impl)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Resolve-once references
// ---------------------------------------------------------------------------

/**
 * @brief Resolve-once reference to an optional caller-owned capability provider.
 *
 * Resolving is a lookup; using one is not. The type is non-copyable, has no
 * default-constructible query and offers no refresh, so a per-frame
 * `cap::query<T>()` cannot hide behind it: resolve at the composition boundary
 * and keep the reference as a member.
 *
 * @ownership Non-owning. For a caller-owned provider (`cap::provide`), the
 *            caller still owns the implementation and must keep it alive while
 *            this reference is in use.
 * @lifetime Valid until the owning member is rebuilt; a revoked or replaced
 *           provider is not detected here (use `ProviderLease` for that).
 * @thread Affine to the thread that resolved it. `cap::query` may be called
 *         concurrently only after registration has settled.
 * @reentrancy Construction and access invoke no callbacks.
 * @remarks A moved-from reference is empty (`operator bool` is false), so a
 *          use-after-move fails visibly instead of silently keeping the provider.
 * @cost One string-keyed registry lookup at construction, amortized over the
 *       owner's lifetime; every later access is a member read. Never per frame.
 */
template <class I>
class ProviderRef {
public:
    /** @brief Empty reference; `operator bool` is false. */
    ProviderRef() noexcept = default;

    ProviderRef(const ProviderRef&)            = delete;
    ProviderRef& operator=(const ProviderRef&) = delete;
    ProviderRef(ProviderRef&& other) noexcept : provider_(other.provider_) { other.provider_ = nullptr; }
    ProviderRef& operator=(ProviderRef&& other) noexcept {
        if (this != &other) {
            provider_       = other.provider_;
            other.provider_ = nullptr;
        }
        return *this;
    }

    /** @brief Resolve the provider registered for `I::capabilityName`; may be empty. */
    [[nodiscard]] static ProviderRef resolve() noexcept { return ProviderRef(query<I>()); }

    /** @brief Whether a provider was present when this reference was resolved. */
    explicit operator bool() const noexcept { return provider_ != nullptr; }

    /**
     * @brief Borrowed provider.
     * @return Borrowed provider pointer, or nullptr when absent.
     * @nullable Yes.
     */
    [[nodiscard]] I* get() const noexcept { return provider_; }

    /** @brief Borrowed provider; only valid after `operator bool` returned true. */
    I* operator->() const noexcept { return provider_; }

private:
    explicit ProviderRef(I* provider) noexcept : provider_(provider) {}
    I* provider_ = nullptr;
};

// ---------------------------------------------------------------------------
// Optional registry-owned providers
// ---------------------------------------------------------------------------
//
// The functions above keep the caller-owned contract: raw pointers, "lifetime is
// the caller's", registration is not thread-safe. That contract cannot carry a
// provider published and retired at runtime -- a plugin, a script-backed handler,
// anything that hot-reloads -- because nothing keeps the implementation alive and
// a stale pointer is indistinguishable from a live one.
//
// This mode is additive: provide/query/addListener/forEach and their semantics are
// unchanged, so every existing registration site is untouched. A provider family
// that needs ownership, generation-qualified staleness or a lease migrates when it
// is next touched (module-boundary spec 8.3).

/**
 * @brief Generation-qualified identity of one registry-owned provider.
 *
 * A handle is stable data, not a pointer: it survives a provider being retired
 * and lets the registry report StaleHandle instead of handing out a recycled
 * implementation.
 */
struct ProviderHandle {
    std::string   capability;
    std::uint64_t generation                                               = 0;
    friend bool   operator==(const ProviderHandle&, const ProviderHandle&) = default;
};

namespace detail {

/** @brief Publish `provider`, superseding any previous generation. @return New generation. */
EVENGINE_API std::uint64_t publishOwnedRaw(const char* name, std::shared_ptr<void> provider);
/** @brief Owning handle for an exact generation, or null when absent/retired/superseded. */
EVENGINE_API std::shared_ptr<void> acquireOwnedRaw(const char* name, std::uint64_t generation);
/** @brief Retire exactly `generation`; false when already retired or superseded. */
EVENGINE_API bool unloadOwnedRaw(const char* name, std::uint64_t generation);
/** @brief Current generation for `name`, or 0 when none is published. */
EVENGINE_API std::uint64_t ownedGenerationRaw(const char* name);

}  // namespace detail

/**
 * @brief Owning, generation-checked reference to one registry-owned provider.
 *
 * The lease keeps the published implementation alive, so retirement may overlap an
 * outstanding holder without the object being destroyed underneath it. Retirement
 * is still visible as `StaleHandle` to anyone acquiring afterwards.
 *
 * @ownership Owns a share of the published implementation until destroyed.
 * @lifetime The implementation outlives every outstanding lease.
 * @thread May be acquired from any thread once registration has settled; the
 *         leased implementation keeps its own thread affinity.
 * @reentrancy Acquiring invokes no provider callbacks and holds no provider lock.
 * @remarks A moved-from lease is empty (`operator bool` is false) and reports no
 *          handle, so a use-after-move cannot silently address the old generation.
 */
template <class I>
class ProviderLease {
public:
    /** @brief Empty lease; `operator bool` is false. */
    ProviderLease() noexcept = default;

    ProviderLease(const ProviderLease&)            = delete;
    ProviderLease& operator=(const ProviderLease&) = delete;
    ProviderLease(ProviderLease&& other) noexcept
        : handle_(std::move(other.handle_)), provider_(std::move(other.provider_)) {
        other.handle_ = ProviderHandle{};
    }
    ProviderLease& operator=(ProviderLease&& other) noexcept {
        if (this != &other) {
            handle_       = std::move(other.handle_);
            provider_     = std::move(other.provider_);
            other.handle_ = ProviderHandle{};
        }
        return *this;
    }

    /**
     * @brief Acquire one exact generation.
     * @return The lease, or StaleHandle when that generation is retired or superseded.
     */
    [[nodiscard]] static Result<ProviderLease> acquire(const ProviderHandle& handle) {
        std::shared_ptr<void> raw = detail::acquireOwnedRaw(handle.capability.c_str(), handle.generation);
        if (!raw) {
            return Result<ProviderLease>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle,
                                  "registry-owned provider generation is no longer published", handle.capability));
        }
        ProviderLease lease;
        lease.handle_   = handle;
        lease.provider_ = std::static_pointer_cast<I>(std::move(raw));
        return Result<ProviderLease>::success(std::move(lease));
    }

    /**
     * @brief Acquire the newest published generation.
     * @return The lease, or NotFound when nothing is published for `I::capabilityName`.
     */
    [[nodiscard]] static Result<ProviderLease> acquire() {
        const std::uint64_t generation = detail::ownedGenerationRaw(I::capabilityName);
        if (generation == 0) {
            return Result<ProviderLease>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "no registry-owned provider is published", I::capabilityName));
        }
        return acquire(ProviderHandle{I::capabilityName, generation});
    }

    /** @brief Identity of the leased generation. */
    const ProviderHandle& handle() const noexcept { return handle_; }
    /** @brief Whether a provider is held. */
    explicit operator bool() const noexcept { return provider_ != nullptr; }
    /**
     * @brief Held provider.
     * @return Borrowed provider pointer, or nullptr when empty.
     * @nullable Yes.
     */
    [[nodiscard]] I* get() const noexcept { return provider_.get(); }
    /** @brief Held provider; only valid after `operator bool` returned true. */
    I* operator->() const noexcept { return provider_.get(); }

private:
    ProviderHandle     handle_{};
    std::shared_ptr<I> provider_{};
};

/**
 * @brief Publish a registry-owned provider.
 * @param provider Shared implementation; the registry keeps it alive.
 * @return The new generation-qualified handle, or InvalidArgument for a null provider.
 * @cost One registry mutation under a lock. Call at load/publish time, never per frame.
 */
template <class I>
[[nodiscard]] Result<ProviderHandle> provideOwned(std::shared_ptr<I> provider) {
    if (!provider) {
        return Result<ProviderHandle>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "registry-owned provider must not be null", I::capabilityName));
    }
    const std::uint64_t generation =
        detail::publishOwnedRaw(I::capabilityName, std::static_pointer_cast<void>(std::move(provider)));
    return Result<ProviderHandle>::success(ProviderHandle{I::capabilityName, generation});
}

/**
 * @brief Retire exactly one registry-owned provider generation.
 * @return Success, or StaleHandle when it was already retired or superseded.
 * @cost One registry mutation under a lock. Call at unload time, never per frame.
 * @remarks An outstanding `ProviderLease` keeps the implementation alive; retirement
 *          only stops new acquisitions.
 */
[[nodiscard]] inline Result<void> unloadOwned(const ProviderHandle& handle) {
    if (detail::unloadOwnedRaw(handle.capability.c_str(), handle.generation)) return Result<void>::success();
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::StaleHandle, "registry-owned provider generation is no longer published", handle.capability));
}

}  // namespace eve::cap
