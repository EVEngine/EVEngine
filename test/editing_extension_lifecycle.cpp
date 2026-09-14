#include "editing/EditingExtension.h"
#include "editor/Editor.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editing;

namespace {
class Provider final : public IEditingExtensionProvider {
public:
    Result<void> activate() override {
        ++activated;
        return eve::editing::applied<void>();
    }
    Result<void> beginUnload() override {
        ++stopping;
        return rejectUnload ? eve::editing::failed<void>(Status::Failed, RuleId("test.unload"), "injected")
                            : eve::editing::applied<void>();
    }
    void deactivate() noexcept override { ++deactivated; }
    void* query(const CapabilityId& capability) noexcept override {
        return capability == CapabilityId("test.capability") ? this : nullptr;
    }
    bool rejectUnload = false;
    int activated = 0;
    int stopping = 0;
    int deactivated = 0;
};
}  // namespace

TEST_CASE("editing.extension.provider_absent_unload_and_stale_handle_are_explicit") {
    ExtensionProviderRegistry registry;
    auto absent = registry.acquire("missing");
    CHECK_EQ(static_cast<int>(absent.code()), static_cast<int>(Status::Unsupported));

    auto provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id = "test.provider";
    descriptor.capabilities = {CapabilityId("test.capability")};
    auto registered = registry.registerProvider(descriptor, provider);
    REQUIRE(registered.ok());
    CHECK_EQ(provider->activated, 1);

    auto lease = registry.acquire(registered.value());
    REQUIRE(lease.ok());
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
    CHECK(registry.unload(registered.value()).ok());
    CHECK_EQ(provider->stopping, 1);
    CHECK_EQ(provider->deactivated, 1);
    auto stale = registry.acquire(registered.value());
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(Status::Conflict));
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
}

TEST_CASE("editing.extension.failed_unload_preserves_current_generation") {
    ExtensionProviderRegistry registry;
    auto provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id = "test.rejecting";
    auto registered = registry.registerProvider(descriptor, provider);
    REQUIRE(registered.ok());
    provider->rejectUnload = true;
    CHECK(!registry.unload(registered.value()).ok());
    CHECK(registry.acquire(registered.value()).ok());
    CHECK_EQ(provider->deactivated, 0);
    provider->rejectUnload = false;
    CHECK(registry.unload(registered.value()).ok());
}

TEST_CASE("editor.composition.discovers_extension_provider_without_domain_type_list") {
    eve::editor::Editor host;
    auto provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id = "test.open-domain-provider";
    descriptor.capabilities = {CapabilityId("test.capability")};

    auto registered = host.extensionProviders().registerProvider(std::move(descriptor), provider);
    REQUIRE(registered.ok());
    const auto descriptors = host.extensionProviders().descriptors();
    REQUIRE_EQ(descriptors.size(), static_cast<std::size_t>(1));
    CHECK_EQ(descriptors.front().id, std::string("test.open-domain-provider"));
    auto lease = host.extensionProviders().acquire(registered.value());
    REQUIRE(lease.ok());
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
}
