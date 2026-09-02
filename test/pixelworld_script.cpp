#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorldModule.h"

using namespace eve::pixelworld;

TEST_CASE("pixelworld.module.creates_seeded_world") {
    auto* module = PixelWorldModule::create();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("PixelWorldModule"));
    PixelWorld* world = module->newWorld(42);
    REQUIRE(world != nullptr);
    CHECK_EQ(world->seed(), std::uint64_t(42));
    CHECK_EQ(world->chunkCount(), 0);
    delete world;
}
