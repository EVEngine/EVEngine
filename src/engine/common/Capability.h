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

#include <cstddef>
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

}  // namespace eve::cap
