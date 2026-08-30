#pragma once

#include "editing/EditingIds.h"
#include "editing/EditingResult.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::editing {

enum class ThreadAffinity { Main, Render, Worker, Any };

struct ExtensionDescriptor {
    std::string id;
    std::uint32_t schemaVersion = 1;
    std::vector<CapabilityId> capabilities;
    std::vector<std::string> requiredModules;
    ThreadAffinity affinity = ThreadAffinity::Main;
    bool callbacksReentrant = false;
};

struct ProviderHandle {
    std::string id;
    std::uint64_t generation = 0;
    friend bool operator==(const ProviderHandle&, const ProviderHandle&) = default;
};

class IEditingExtensionProvider {
public:
    virtual ~IEditingExtensionProvider() = default;
    /** @brief Activate after publication. No registry lock is held. */
    [[nodiscard]] virtual Result<void> activate() = 0;
    /** @brief Stop accepting new work before the provider is unpublished. */
    [[nodiscard]] virtual Result<void> beginUnload() = 0;
    /** @brief Release provider-side resources after unpublication. */
    virtual void deactivate() noexcept = 0;
    /** @brief Query a borrowed capability while this provider lease is alive.
     * @return Borrowed provider-owned capability, or null when unsupported.
     * @lifetime Valid only while the calling ProviderLease remains alive.
     */
    virtual void* query(const CapabilityId& capability) noexcept = 0;
};

class ProviderLease {
public:
    ProviderLease() = default;
    const ProviderHandle& handle() const { return handle_; }
    explicit operator bool() const { return provider_ != nullptr; }
    /** @brief Query a capability.
     * @return Borrowed provider-owned capability, or null when unsupported.
     * @lifetime Valid only while this lease remains alive.
     */
    void* query(const CapabilityId& capability) const noexcept {
        return provider_ ? provider_->query(capability) : nullptr;
    }
private:
    friend class ExtensionProviderRegistry;
    ProviderLease(ProviderHandle handle, std::shared_ptr<IEditingExtensionProvider> provider)
        : handle_(std::move(handle)), provider_(std::move(provider)) {}
    ProviderHandle handle_;
    std::shared_ptr<IEditingExtensionProvider> provider_;
};

/** @brief Thread-safe generation-qualified registry for editing extension providers. */
class ExtensionProviderRegistry {
public:
    ExtensionProviderRegistry();
    ~ExtensionProviderRegistry();
    ExtensionProviderRegistry(const ExtensionProviderRegistry&) = delete;
    ExtensionProviderRegistry& operator=(const ExtensionProviderRegistry&) = delete;

    /** @brief Publish and activate a provider atomically from the caller's perspective. */
    [[nodiscard]] Result<ProviderHandle> registerProvider(
        ExtensionDescriptor descriptor, std::shared_ptr<IEditingExtensionProvider> provider);
    /** @brief Acquire an owning lease or return Unsupported/StaleHandle diagnostics. */
    [[nodiscard]] Result<ProviderLease> acquire(const ProviderHandle& handle) const;
    /** @brief Acquire the current generation by id; missing providers are explicit. */
    [[nodiscard]] Result<ProviderLease> acquire(const std::string& id) const;
    /** @brief Stop, unpublish and deactivate the exact provider generation. */
    [[nodiscard]] Result<void> unload(const ProviderHandle& handle);
    /** @brief Return owning copies of current descriptors. */
    std::vector<ExtensionDescriptor> descriptors() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Register one owned capability behind the standard provider lifecycle.
 * @param registry Destination registry owned by the host.
 * @param descriptor Owning extension description; capabilities must contain capability.
 * @param capability Capability id returned by the provider.
 * @param implementation Type-erased owning capability implementation.
 * @return Generation-qualified provider handle.
 */
[[nodiscard]] Result<ProviderHandle> registerStaticProvider(
    ExtensionProviderRegistry& registry, ExtensionDescriptor descriptor,
    CapabilityId capability, std::shared_ptr<void> implementation);

}  // namespace eve::editing
