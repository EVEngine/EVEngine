#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "definitions/Definitions.h"
#include "spawn/definitions/SpawnTemplateSource.h"

namespace test_spawn_definitions {
namespace {
class Units final : public eve::spawn::IUnitDefinitionResolver {
public:
    eve::Result<void> validate(const eve::DefinitionRef&) const override { return eve::Result<void>::success(); }
};
eve::Value asset() {
    return eve::Value::fromJson(R"({
      "schema":"eve.spawn-template", "version":1, "id":"guards",
      "content":{"kind":"fixed","members":[{"id":"guard","unit":{"type":"rpg.unit","id":"guard"},"count":2}]},
      "schedule":{"kind":"once"},
      "limits":{"alive":2,"total":{"kind":"finite","count":2},"budget_group":"forest"},
      "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":12},
      "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":2}}
    })")
        .expect("valid template source");
}
}  // namespace
TEST_CASE("Spawn definitions publication preserves old revisions and rejects invalid replacement") {
    eve::definitions::DefinitionRegistry registry;
    Units                                units;
    eve::spawn::SpawnTemplateSource      source(registry, units);
    auto                                 content = asset();
    REQUIRE(source.publish(content).ok());
    auto reference = eve::DefinitionRef::parse("spawn.template:guards").expect("reference");
    auto original  = source.resolve(reference);
    REQUIRE(original.ok());
    auto oldHandle = registry.handle("spawn.template", "guards");
    REQUIRE(oldHandle.ok());
    content.find("limits")->set("alive", eve::Value(3));
    REQUIRE(source.publish(content).ok());
    CHECK(registry.isStale(oldHandle.value()));
    auto current = source.resolve(reference);
    REQUIRE(current.ok());
    CHECK_EQ(original.value().definition().limits.alive, std::uint32_t(2));
    CHECK_EQ(current.value().definition().limits.alive, std::uint32_t(3));
    content.find("limits")->set("alive", eve::Value(1));
    CHECK(!source.publish(content).ok());
    auto unchanged = source.resolve(reference);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().canonicalJson(), current.value().canonicalJson());
    auto removed = registry.remove("spawn.template", "guards");
    REQUIRE(removed.ok());
    CHECK(!source.resolve(reference).ok());
    CHECK_EQ(original.value().definition().limits.alive, std::uint32_t(2));
}
}  // namespace test_spawn_definitions
