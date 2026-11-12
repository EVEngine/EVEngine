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
#include "common/ProviderRegistry.h"
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

    /** @brief Share the provider and its generation identity with @p other. */
    OwnedProviderLease(const OwnedProviderLease& other) = default;
    /** @brief Share @p other, releasing this lease's previous ownership. @return This lease. */
    OwnedProviderLease& operator=(const OwnedProviderLease& other) = default;
    /** @brief Transfer ownership and identity, leaving @p other empty with generation zero. */
    OwnedProviderLease(OwnedProviderLease&& other) noexcept
        : handle_(std::exchange(other.handle_, OwnedProviderHandle{})), provider_(std::move(other.provider_)) {}
    /** @brief Transfer @p other and empty it; self-move preserves this lease. @return This lease. */
    OwnedProviderLease& operator=(OwnedProviderLease&& other) noexcept {
        if (this != &other) {
            handle_   = std::exchange(other.handle_, OwnedProviderHandle{});
            provider_ = std::move(other.provider_);
        }
        return *this;
    }

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

/** @brief Process-owned type-erased store; one canonical instance across link groups. */
EVENGINE_API_FOUNDATION std::mutex& ownedPublicationMutex();
EVENGINE_API_FOUNDATION ProviderRegistry<void>& ownedRegistry(const char* capabilityName);
/** @brief Clear publications for isolated tests after borrowed dispatch has stopped. */
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
        std::unique_lock lock(detail::ownedPublicationMutex());
        auto             result = detail::ownedRegistry(I::capabilityName).publish(std::move(id), provider);
        if (!result.ok()) return result;
        detail::provideRaw(I::capabilityName, provider.get());
        lock.unlock();
        return result;
    }

    /**
     * @brief Acquire an owning lease for an exact handle generation.
     * @return Unsupported when absent, Conflict when the generation is stale.
     */
    [[nodiscard]] static Result<OwnedProviderLease<I>> acquire(const OwnedProviderHandle& handle) {
        auto record = detail::ownedRegistry(I::capabilityName).acquire(handle);
        if (!record.ok()) return Result<OwnedProviderLease<I>>::failure(record.status());
        return Result<OwnedProviderLease<I>>::success(
            OwnedProviderLease<I>(record.value().handle, std::static_pointer_cast<I>(record.value().provider)),
            Status::success(StatusCode::Applied));
    }

    /**
     * @brief Acquire the current generation published under `id`.
     */
    [[nodiscard]] static Result<OwnedProviderLease<I>> acquire(std::string_view id) {
        auto record = detail::ownedRegistry(I::capabilityName).acquire(id);
        if (!record.ok()) return Result<OwnedProviderLease<I>>::failure(record.status());
        return Result<OwnedProviderLease<I>>::success(
            OwnedProviderLease<I>(record.value().handle, std::static_pointer_cast<I>(record.value().provider)),
            Status::success(StatusCode::Applied));
    }

    /**
     * @brief Unpublish one exact provider generation and clear the borrowed slot when it matches.
     */
    [[nodiscard]] static Result<void> unload(const OwnedProviderHandle& handle) {
        std::unique_lock lock(detail::ownedPublicationMutex());
        auto             removed = detail::ownedRegistry(I::capabilityName).withdraw(handle);
        if (!removed.ok()) return Result<void>::failure(removed.status());
        detail::revokeRaw(I::capabilityName, removed.value().provider.get());
        lock.unlock();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

}  // namespace eve::cap
