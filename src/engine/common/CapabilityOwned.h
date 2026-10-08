#pragma once

/**
 * @file CapabilityOwned.h
 * @brief Optional owning capability registration (shared_ptr + generation + lease).
 *
 * Elevates the model validated by `editing::ExtensionProviderRegistry` into the
 * common capability layer (`R-COHESION-3`). Bare `provide`/`query` remain the
 * default borrowed path; this header is for families that need ownership,
 * stale-handle detection, or thread-safe register/unload.
 */

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Status.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace eve::cap {

/**
 * @brief Generation-qualified id for one owned capability provider.
 *
 * @ownership Caller-owned value; the registry owns the implementation behind it.
 * @lifetime Remains acquireable until `unloadOwned` succeeds for the same generation.
 */
struct OwnedProviderHandle {
    std::string   id;
    std::uint64_t generation = 0;

    [[nodiscard]] friend bool operator==(const OwnedProviderHandle&, const OwnedProviderHandle&) = default;
};

/**
 * @brief Owning lease that keeps a `shared_ptr` provider alive while borrowed.
 *
 * @ownership Shared with the registry until unload completes; the lease prolongs lifetime.
 * @lifetime Valid until destroyed; do not retain the raw `get()` pointer after the lease ends.
 * @thread Acquire under the registry lock; subsequent `get()` is a local shared_ptr read.
 */
template <class I>
class OwnedProviderLease {
public:
    OwnedProviderLease() = default;

    [[nodiscard]] const OwnedProviderHandle& handle() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(provider_); }
    /** @brief Borrowed interface pointer valid while this lease remains alive. */
    [[nodiscard]] I* get() const noexcept { return provider_.get(); }
    [[nodiscard]] I& operator*() const noexcept { return *provider_; }
    [[nodiscard]] I* operator->() const noexcept { return provider_.get(); }

private:
    template <class>
    friend class OwnedProviderRegistry;

    OwnedProviderLease(OwnedProviderHandle handle, std::shared_ptr<I> provider)
        : handle_(std::move(handle)), provider_(std::move(provider)) {}

    OwnedProviderHandle handle_;
    std::shared_ptr<I>  provider_;
};

namespace detail {

struct OwnedEntryBase {
    virtual ~OwnedEntryBase()                           = default;
    virtual void*       raw() noexcept                  = 0;
    virtual std::shared_ptr<void> shared() const noexcept = 0;
};

template <class I>
struct OwnedEntry final : OwnedEntryBase {
    explicit OwnedEntry(std::shared_ptr<I> provider) : provider(std::move(provider)) {}
    void* raw() noexcept override { return provider.get(); }
    std::shared_ptr<void> shared() const noexcept override { return provider; }
    std::shared_ptr<I> provider;
};

struct OwnedSlot {
    OwnedProviderHandle             handle;
    std::unique_ptr<OwnedEntryBase> entry;
};

EVENGINE_API_FOUNDATION std::mutex& ownedMutex();
EVENGINE_API_FOUNDATION std::unordered_map<std::string, OwnedSlot>& ownedSlots(const char* capabilityName);
EVENGINE_API_FOUNDATION std::unordered_map<std::string, std::uint64_t>& ownedGenerations(const char* capabilityName);
EVENGINE_API_FOUNDATION void clearOwnedRaw();

}  // namespace detail

/**
 * @brief Thread-safe owned-provider table keyed by capability name + provider id.
 *
 * @cost Register/acquire/unload take the owned-registry mutex; keep out of hot paths.
 */
template <class I>
class OwnedProviderRegistry {
public:
    /**
     * @brief Publish an owning provider under `id`, replacing only after explicit unload.
     * @param id Non-empty stable provider id within capability `I`.
     * @param provider Non-null shared ownership of the implementation.
     * @return Generation-qualified handle, or Conflict when `id` is already published.
     */
    [[nodiscard]] static Result<OwnedProviderHandle> provide(std::string id, std::shared_ptr<I> provider) {
        if (id.empty() || !provider) {
            return Result<OwnedProviderHandle>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "owned capability requires id and implementation", "id"));
        }
        std::scoped_lock lock(detail::ownedMutex());
        auto&            slots = detail::ownedSlots(I::capabilityName);
        if (slots.contains(id)) {
            return Result<OwnedProviderHandle>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "owned capability id is already published", "id"));
        }
        auto& generations = detail::ownedGenerations(I::capabilityName);
        OwnedProviderHandle handle{id, ++generations[id]};
        auto [it, inserted] =
            slots.emplace(std::move(id), detail::OwnedSlot{handle, std::make_unique<detail::OwnedEntry<I>>(
                                                                         std::move(provider))});
        (void)inserted;
        // Also expose the current generation through the borrowed single-provider slot so
        // existing `query` / `ProviderRef` consumers see the same instance.
        detail::provideRaw(I::capabilityName, it->second.entry->raw());
        return Result<OwnedProviderHandle>::success(std::move(handle), Status::success(StatusCode::Applied));
    }

    /**
     * @brief Acquire an owning lease for an exact handle generation.
     * @return Unsupported when absent, Conflict when the generation is stale.
     */
    [[nodiscard]] static Result<OwnedProviderLease<I>> acquire(const OwnedProviderHandle& handle) {
        std::scoped_lock lock(detail::ownedMutex());
        auto&            slots = detail::ownedSlots(I::capabilityName);
        const auto       found = slots.find(handle.id);
        if (found == slots.end()) {
            const auto& generations = detail::ownedGenerations(I::capabilityName);
            const bool  seen        = generations.contains(handle.id);
            return Result<OwnedProviderLease<I>>::failure(Diagnostic::error(
                seen ? DiagnosticCode::Conflict : DiagnosticCode::Unsupported,
                seen ? "owned capability handle is stale" : "owned capability provider is not available", "handle"));
        }
        if (found->second.handle != handle) {
            return Result<OwnedProviderLease<I>>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "owned capability handle is stale", "handle"));
        }
        auto typed = std::static_pointer_cast<I>(found->second.entry->shared());
        return Result<OwnedProviderLease<I>>::success(OwnedProviderLease<I>(handle, std::move(typed)),
                                                      Status::success(StatusCode::Applied));
    }

    /**
     * @brief Acquire the current generation published under `id`.
     */
    [[nodiscard]] static Result<OwnedProviderLease<I>> acquire(std::string_view id) {
        std::scoped_lock lock(detail::ownedMutex());
        auto&            slots = detail::ownedSlots(I::capabilityName);
        const auto       found = slots.find(std::string(id));
        if (found == slots.end()) {
            return Result<OwnedProviderLease<I>>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "owned capability provider is not available", "id"));
        }
        auto typed = std::static_pointer_cast<I>(found->second.entry->shared());
        return Result<OwnedProviderLease<I>>::success(
            OwnedProviderLease<I>(found->second.handle, std::move(typed)), Status::success(StatusCode::Applied));
    }

    /**
     * @brief Unpublish one exact provider generation and clear the borrowed slot when it matches.
     */
    [[nodiscard]] static Result<void> unload(const OwnedProviderHandle& handle) {
        std::shared_ptr<void> keepAlive;
        {
            std::scoped_lock lock(detail::ownedMutex());
            auto&            slots = detail::ownedSlots(I::capabilityName);
            const auto       found = slots.find(handle.id);
            if (found == slots.end() || found->second.handle != handle) {
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "owned capability handle is stale", "handle"));
            }
            void* raw = found->second.entry->raw();
            keepAlive = found->second.entry->shared();
            slots.erase(found);
            detail::revokeRaw(I::capabilityName, raw);
        }
        (void)keepAlive;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

}  // namespace eve::cap
