#include "editing/EditingExtension.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace eve::editing {
namespace {
template <class T>
Result<T> failure(Status status, const char* rule, std::string message) {
    return Result<T>::error(status, RuleId(rule), std::move(message));
}
}  // namespace

struct ExtensionProviderRegistry::Impl {
    struct Entry {
        ExtensionDescriptor descriptor;
        ProviderHandle handle;
        std::shared_ptr<IEditingExtensionProvider> provider;
    };
    mutable std::mutex mutex;
    std::unordered_map<std::string, Entry> entries;
    std::unordered_map<std::string, std::uint64_t> generations;
};

ExtensionProviderRegistry::ExtensionProviderRegistry() : impl_(std::make_unique<Impl>()) {}

ExtensionProviderRegistry::~ExtensionProviderRegistry() {
    std::vector<std::shared_ptr<IEditingExtensionProvider>> providers;
    {
        std::scoped_lock lock(impl_->mutex);
        for (auto& [id, entry] : impl_->entries) providers.push_back(entry.provider);
        impl_->entries.clear();
    }
    for (auto& provider : providers) {
        auto stopped = provider->beginUnload();
        (void)stopped;
        provider->deactivate();
    }
}

Result<ProviderHandle> ExtensionProviderRegistry::registerProvider(
    ExtensionDescriptor descriptor, std::shared_ptr<IEditingExtensionProvider> provider) {
    if (descriptor.id.empty() || descriptor.schemaVersion == 0 || !provider)
        return failure<ProviderHandle>(Status::Rejected, "editing.extension.invalid-provider",
                                       "Extension provider requires id, schema version and implementation");
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->entries.contains(descriptor.id))
            return failure<ProviderHandle>(Status::Conflict, "editing.extension.duplicate-provider",
                                           "Extension provider id is already published");
    }
    auto activated = provider->activate();
    if (!activated.isAccepted())
        return failure<ProviderHandle>(Status::Failed, "editing.extension.activation-failed",
                                       "Extension provider activation failed");
    ProviderHandle handle;
    bool duplicate = false;
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->entries.contains(descriptor.id)) {
            duplicate = true;
        } else {
            const std::string providerId = descriptor.id;
            handle = {providerId, ++impl_->generations[providerId]};
            impl_->entries.emplace(providerId, Impl::Entry{std::move(descriptor), handle, provider});
        }
    }
    if (duplicate) {
        provider->deactivate();
        return failure<ProviderHandle>(Status::Conflict, "editing.extension.duplicate-provider",
                                       "Extension provider id was concurrently published");
    }
    return Result<ProviderHandle>::applied(handle);
}

Result<ProviderLease> ExtensionProviderRegistry::acquire(const ProviderHandle& handle) const {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->entries.find(handle.id);
    if (found == impl_->entries.end()) {
        const auto generation = impl_->generations.find(handle.id);
        return failure<ProviderLease>(
            generation == impl_->generations.end() ? Status::Unsupported : Status::Conflict,
            generation == impl_->generations.end() ? "editing.extension.provider-absent"
                                                   : "editing.extension.stale-handle",
            generation == impl_->generations.end() ? "Editing extension provider is not available"
                                                   : "Editing extension handle is stale");
    }
    if (found->second.handle != handle)
        return failure<ProviderLease>(Status::Conflict, "editing.extension.stale-handle",
                                      "Editing extension handle is stale");
    return Result<ProviderLease>::applied(ProviderLease(handle, found->second.provider));
}

Result<ProviderLease> ExtensionProviderRegistry::acquire(const std::string& id) const {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->entries.find(id);
    if (found == impl_->entries.end())
        return failure<ProviderLease>(Status::Unsupported, "editing.extension.provider-absent",
                                      "Editing extension provider is not available");
    return Result<ProviderLease>::applied(ProviderLease(found->second.handle, found->second.provider));
}

Result<void> ExtensionProviderRegistry::unload(const ProviderHandle& handle) {
    std::shared_ptr<IEditingExtensionProvider> provider;
    {
        std::scoped_lock lock(impl_->mutex);
        const auto found = impl_->entries.find(handle.id);
        if (found == impl_->entries.end() || found->second.handle != handle)
            return failure<void>(Status::Conflict, "editing.extension.stale-handle",
                                 "Editing extension handle is stale");
        provider = found->second.provider;
    }
    auto stopping = provider->beginUnload();
    if (!stopping.isAccepted())
        return failure<void>(Status::Failed, "editing.extension.unload-rejected",
                             "Editing extension refused to stop accepting work");
    {
        std::scoped_lock lock(impl_->mutex);
        const auto found = impl_->entries.find(handle.id);
        if (found == impl_->entries.end() || found->second.handle != handle)
            return failure<void>(Status::Conflict, "editing.extension.stale-handle",
                                 "Editing extension handle is stale");
        impl_->entries.erase(found);
    }
    provider->deactivate();
    return Result<void>::applied();
}

std::vector<ExtensionDescriptor> ExtensionProviderRegistry::descriptors() const {
    std::vector<ExtensionDescriptor> result;
    std::scoped_lock lock(impl_->mutex);
    for (const auto& [id, entry] : impl_->entries) result.push_back(entry.descriptor);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}

namespace {
class StaticProvider final : public IEditingExtensionProvider {
public:
    StaticProvider(CapabilityId capability, std::shared_ptr<void> implementation)
        : capability_(std::move(capability)), implementation_(std::move(implementation)) {}

    Result<void> activate() override {
        accepting_.store(true, std::memory_order_release);
        return Result<void>::applied();
    }
    Result<void> beginUnload() override {
        accepting_.store(false, std::memory_order_release);
        return Result<void>::applied();
    }
    void deactivate() noexcept override { accepting_.store(false, std::memory_order_release); }
    void* query(const CapabilityId& capability) noexcept override {
        return accepting_.load(std::memory_order_acquire) && capability == capability_
                   ? implementation_.get()
                   : nullptr;
    }

private:
    CapabilityId capability_;
    std::shared_ptr<void> implementation_;
    std::atomic_bool accepting_{false};
};
}  // namespace

Result<ProviderHandle> registerStaticProvider(ExtensionProviderRegistry& registry,
                                              ExtensionDescriptor descriptor,
                                              CapabilityId capability,
                                              std::shared_ptr<void> implementation) {
    if (!implementation)
        return failure<ProviderHandle>(Status::Rejected, "editing.extension.null-capability",
                                       "Static editing provider requires an owned capability");
    if (std::find(descriptor.capabilities.begin(), descriptor.capabilities.end(), capability) ==
        descriptor.capabilities.end())
        descriptor.capabilities.push_back(capability);
    return registry.registerProvider(
        std::move(descriptor),
        std::make_shared<StaticProvider>(std::move(capability), std::move(implementation)));
}
}  // namespace eve::editing
