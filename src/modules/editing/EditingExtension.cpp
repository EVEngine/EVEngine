#include "editing/EditingExtension.h"
#include <unordered_set>
#include "common/ProviderRegistry.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace eve::editing {
namespace {}  // namespace

struct ExtensionProviderRegistry::Impl {
    eve::cap::ProviderRegistry<IEditingExtensionProvider, ExtensionDescriptor> providers;
    std::mutex                                                                 mutex;
    std::unordered_set<std::string>                                            changing;
    bool                                                                       closing = false;

    struct Change {
        Impl&       owner;
        std::string id;
        ~Change() {
            std::scoped_lock lock(owner.mutex);
            owner.changing.erase(id);
        }
    };
};

ExtensionProviderRegistry::ExtensionProviderRegistry() : impl_(std::make_unique<Impl>()) {}

ExtensionProviderRegistry::~ExtensionProviderRegistry() {
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->closing = true;
    }
    for (const auto& entry : impl_->providers.snapshot()) {
        impl_->providers.withdraw(entry.handle).ignore("editing extension registry teardown");
        entry.provider->beginUnload().ignore("editing extension registry teardown");
        entry.provider->deactivate();
    }
}

Result<ProviderHandle> ExtensionProviderRegistry::registerProvider(
    ExtensionDescriptor descriptor, std::shared_ptr<IEditingExtensionProvider> provider) {
    if (descriptor.id.empty() || descriptor.schemaVersion == 0 || !provider)
        return eve::editing::failed<ProviderHandle>(
            Status::Rejected, RuleId("editing.extension.invalid-provider"),
            "Extension provider requires id, schema version and implementation");
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->closing)
            return eve::editing::failed<ProviderHandle>(Status::Conflict, RuleId("editing.extension.shutting-down"),
                                                        "Extension registry is shutting down");
        auto existing = impl_->providers.acquire(descriptor.id);
        if (existing.ok() || !impl_->changing.insert(descriptor.id).second)
            return eve::editing::failed<ProviderHandle>(Status::Conflict,
                                                        RuleId("editing.extension.duplicate-provider"),
                                                        "Extension provider id is published or changing");
    }
    Impl::Change change{*impl_, descriptor.id};
    auto activated = provider->activate();
    if (!activated.ok())
        return eve::editing::failed<ProviderHandle>(Status::Failed, RuleId("editing.extension.activation-failed"),
                                                    "Extension provider activation failed");
    const auto id        = descriptor.id;
    auto       published = impl_->providers.publish(id, provider, std::move(descriptor));
    if (!published.ok()) {
        provider->deactivate();
        return eve::editing::failed<ProviderHandle>(Status::Conflict, RuleId("editing.extension.duplicate-provider"),
                                                    "Extension provider publication failed");
    }
    return eve::editing::applied<ProviderHandle>({published.value().id, published.value().generation});
}

Result<ProviderLease> ExtensionProviderRegistry::acquire(const ProviderHandle& handle) const {
    auto entry = impl_->providers.acquire(eve::cap::OwnedProviderHandle{handle.id, handle.generation});
    if (!entry.ok()) {
        const bool absent = entry.status().code() == eve::StatusCode::Unsupported;
        return eve::editing::failed<ProviderLease>(
            absent ? Status::Unsupported : Status::Conflict,
            RuleId(absent ? "editing.extension.provider-absent" : "editing.extension.stale-handle"),
            "Editing extension provider is absent or stale");
    }
    return eve::editing::applied<ProviderLease>(ProviderLease(handle, entry.value().provider));
}

Result<ProviderLease> ExtensionProviderRegistry::acquire(const std::string& id) const {
    auto entry = impl_->providers.acquire(id);
    if (!entry.ok())
        return eve::editing::failed<ProviderLease>(Status::Unsupported, RuleId("editing.extension.provider-absent"),
                                                   "Editing extension provider is not available");
    const auto& record = entry.value();
    return eve::editing::applied<ProviderLease>(
        ProviderLease({record.handle.id, record.handle.generation}, record.provider));
}

Result<void> ExtensionProviderRegistry::unload(const ProviderHandle& handle) {
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->changing.insert(handle.id).second)
            return eve::editing::failed<void>(Status::Conflict, RuleId("editing.extension.unload-rejected"),
                                              "Editing extension lifecycle transition is already in progress");
    }
    Impl::Change change{*impl_, handle.id};
    auto         entry = impl_->providers.acquire(eve::cap::OwnedProviderHandle{handle.id, handle.generation});
    if (!entry.ok())
        return eve::editing::failed<void>(Status::Conflict, RuleId("editing.extension.stale-handle"),
                                          "Editing extension handle is stale");
    auto stopping = entry.value().provider->beginUnload();
    if (!stopping.ok())
        return eve::editing::failed<void>(Status::Failed, RuleId("editing.extension.unload-rejected"),
                                          "Editing extension refused to stop accepting work");
    auto removed = impl_->providers.withdraw(eve::cap::OwnedProviderHandle{handle.id, handle.generation});
    if (!removed.ok())
        return eve::editing::failed<void>(Status::Conflict, RuleId("editing.extension.stale-handle"),
                                          "Editing extension handle is stale");
    removed.value().provider->deactivate();
    return eve::editing::applied<void>();
}

std::vector<ExtensionDescriptor> ExtensionProviderRegistry::descriptors() const {
    std::vector<ExtensionDescriptor> result;
    for (const auto& entry : impl_->providers.snapshot()) result.push_back(entry.metadata);
    return result;
}

namespace {
class StaticProvider final : public IEditingExtensionProvider {
public:
    StaticProvider(CapabilityId capability, std::shared_ptr<void> implementation)
        : capability_(std::move(capability)), implementation_(std::move(implementation)) {}

    Result<void> activate() override {
        accepting_.store(true, std::memory_order_release);
        return eve::editing::applied<void>();
    }
    Result<void> beginUnload() override {
        accepting_.store(false, std::memory_order_release);
        return eve::editing::applied<void>();
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
        return eve::editing::failed<ProviderHandle>(Status::Rejected, RuleId("editing.extension.null-capability"),
                                                    "Static editing provider requires an owned capability");
    if (std::find(descriptor.capabilities.begin(), descriptor.capabilities.end(), capability) ==
        descriptor.capabilities.end())
        descriptor.capabilities.push_back(capability);
    return registry.registerProvider(
        std::move(descriptor),
        std::make_shared<StaticProvider>(std::move(capability), std::move(implementation)));
}
}  // namespace eve::editing
