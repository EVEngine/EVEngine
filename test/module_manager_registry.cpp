#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include "common/Module.h"

namespace {
class RegistryProbe final : public eve::Module {
public:
    inline static int live = 0;
    RegistryProbe() { ++live; }
    ~RegistryProbe() override { --live; }
    std::string         getName() const override { return "RegistryStorageProbe"; }
    static eve::Module* create() {
        auto* instance = new RegistryProbe;
        eve::ModuleManager::insert("RegistryStorageProbe", instance);
        return instance;
    }
};
}  // namespace

TEST_CASE("ModuleManager.RegistryRollbackOwnsCreatedInstance") {
    using eve::ModuleManager;
    auto started = ModuleManager::beginPluginRegistration();
    REQUIRE(started.ok());
    ModuleManager::register_module("RegistryStorageProbe", &RegistryProbe::create, nullptr);
    auto* instance = ModuleManager::requireInstance<RegistryProbe>("RegistryStorageProbe");
    REQUIRE(instance != nullptr);
    CHECK_EQ(ModuleManager::find("RegistryStorageProbe"), instance);
    CHECK_EQ(RegistryProbe::live, 1);
    auto nested = ModuleManager::beginPluginRegistration();
    CHECK(!nested.ok());
    auto rolledBack = ModuleManager::finishPluginRegistration(false);
    REQUIRE(rolledBack.ok());
    CHECK_EQ(RegistryProbe::live, 0);
    CHECK(ModuleManager::find("RegistryStorageProbe") == nullptr);
    auto inactive = ModuleManager::finishPluginRegistration(true);
    CHECK(!inactive.ok());
}

TEST_CASE("ModuleManager.RegistryConflictPreservesCommittedInstance") {
    using eve::ModuleManager;
    auto started = ModuleManager::beginPluginRegistration();
    REQUIRE(started.ok());
    ModuleManager::register_module("RegistryStorageProbe", &RegistryProbe::create, nullptr);
    auto* instance  = ModuleManager::requireInstance<RegistryProbe>("RegistryStorageProbe");
    auto  committed = ModuleManager::finishPluginRegistration(true);
    REQUIRE(committed.ok());
    auto again = ModuleManager::beginPluginRegistration();
    REQUIRE(again.ok());
    ModuleManager::register_module("RegistryStorageProbe", &RegistryProbe::create, nullptr);
    auto conflicted = ModuleManager::finishPluginRegistration(true);
    CHECK(!conflicted.ok());
    CHECK_EQ(ModuleManager::find("RegistryStorageProbe"), instance);
    CHECK_EQ(RegistryProbe::live, 1);
}
