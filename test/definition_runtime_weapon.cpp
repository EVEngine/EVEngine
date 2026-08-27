#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "weapon/WeaponDefinitionRuntime.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace {

eve::PersistentId testId() {
    const auto parsed = eve::PersistentId::parse("33333333-3333-7333-8333-333333333333");
    return *parsed;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint8_t value = 41;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            for (const unsigned char byte : input)
                value = static_cast<std::uint8_t>(value * 33u + byte + index);
            bytes[index] = static_cast<std::uint8_t>(value + index);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::DefinitionRef weaponRef() {
    auto result = eve::DefinitionRef::parse("weapon:rifle");
    if (!result.ok()) return {};
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("definitionRuntime.weaponTypedStateSnapshotReloadAndStale") {
    eve::definitions::DefinitionRegistry registry;
    const std::string original = R"JSON({
        "id":"rifle", "kind":"ranged", "logic":"hitscan", "damage":25,
        "range":500, "spreadMin":1, "spreadMax":8,
        "ammo":{"mag":30,"reserve":90,"reload":2},
        "resource":{"kind":"ammo","max":30,"cost":1,"infinite":false},
        "projectile":{"type":"bullet","speed":800,"pelletCount":1}
    })JSON";
    auto inserted = registry.insert("weapon", "rifle", 1, original);
    REQUIRE(inserted.ok());

    auto created = eve::weapon::WeaponDefinitionRuntime::create(
        registry, weaponRef(), testId(), eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(created.ok());
    auto runtime = std::move(created).takeValue();
    const auto oldHandle = runtime.definitionHandle();
    auto typed = runtime.definition();
    REQUIRE(typed.ok());
    CHECK_EQ(typed.value().damage, 25.f);
    CHECK_EQ(typed.value().projectile.type, std::string("bullet"));
    CHECK_EQ(runtime.state().resource.value, 30.f);
    runtime.state().resource.value = 7.f;
    runtime.state().currentSpread = 4.f;

    auto snapshot = runtime.snapshot(eve::Revision(2), eve::SimulationTick(17), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().resource.value = 1.f;
    runtime.state().currentSpread = 0.f;
    auto restored = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().resource.value, 7.f);
    CHECK_EQ(runtime.state().currentSpread, 4.f);

    const std::string replacement = R"JSON({
        "id":"rifle", "kind":"ranged", "logic":"hitscan", "damage":40,
        "range":700, "spreadMin":2, "spreadMax":3,
        "ammo":{"mag":10,"reserve":20,"reload":1}
    })JSON";
    auto replaced = registry.replace("weapon", "rifle", 1, replacement);
    REQUIRE(replaced.ok());
    auto reloaded = runtime.reload(eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(reloaded.ok());
    CHECK(runtime.definitionHandle().generation != oldHandle.generation);
    CHECK_EQ(runtime.state().resource.value, 7.f);
    CHECK_EQ(runtime.state().currentSpread, 3.f);
    auto stale = runtime.restore(snapshot.value(), testHash());
    CHECK(!stale.ok());
    CHECK_EQ(runtime.state().resource.value, 7.f);
}

TEST_CASE("definitionRuntime.weaponRejectWhileActiveIsAtomic") {
    eve::definitions::DefinitionRegistry registry;
    const auto first = registry.insert("weapon", "rifle", 1,
                                       R"JSON({"id":"rifle","kind":"ranged","ammo":{"mag":4}})JSON");
    REQUIRE(first.ok());
    auto created = eve::weapon::WeaponDefinitionRuntime::create(registry, weaponRef(), testId());
    REQUIRE(created.ok());
    auto runtime = std::move(created).takeValue();
    runtime.setActive(true);
    const auto before = runtime.definitionHandle();
    const auto replacement = registry.replace(
        "weapon", "rifle", 1, R"JSON({"id":"rifle","kind":"ranged","ammo":{"mag":8}})JSON");
    REQUIRE(replacement.ok());
    auto rejected = runtime.reload(eve::definition::ReloadPolicy::RejectWhileActive);
    CHECK(!rejected.ok());
    CHECK_EQ(runtime.definitionHandle().generation, before.generation);
}
