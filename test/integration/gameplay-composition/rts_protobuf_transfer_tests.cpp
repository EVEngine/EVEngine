#include "definitions/Definitions.h"
#include "rts/RTSContent.h"
#include "rts/RTSTypes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("rts.protobufContentImportKeepsRegistryOnRejection") {
    eve::definitions::DefinitionRegistry registry;
    auto                                 loaded =
        eve::rts::RTSContentLoader::load(registry, R"JSON({"buildings":[{"id":"barracks","health":700}]})JSON");
    REQUIRE(loaded.ok());

    auto before = registry.snapshotBinary();
    REQUIRE(before.ok());
    auto rejected =
        eve::rts::RTSContentLoader::load(registry, R"JSON({"units":[{"id":"broken","weaponType":"missing"}]})JSON");
    CHECK(!rejected.ok());
    rejected.ignore("expected atomic RTS content rejection");
    auto after = registry.snapshotBinary();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
}

TEST_CASE("rts.productionComponentCopyPreservesTypedQueue") {
    eve::rts::ProductionComponent source;
    auto                          task = source.enqueue("owner", "build", "product",
                                                        eve::Duration::fromSeconds(2.0).expect("RTS production copy duration"), 3);
    REQUIRE(task.ok());
    eve::rts::ProductionComponent copied(source);
    eve::rts::ProductionComponent assigned;
    assigned            = source;
    auto original       = source.snapshot();
    auto fromCopy       = copied.snapshot();
    auto fromAssignment = assigned.snapshot();
    REQUIRE(original.ok());
    REQUIRE(fromCopy.ok());
    REQUIRE(fromAssignment.ok());
    CHECK_EQ(fromCopy.value(), original.value());
    CHECK_EQ(fromAssignment.value(), original.value());
}
