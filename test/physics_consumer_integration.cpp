#include "physics/ArtifactProvider.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/physics/PixelWorldPhysics.h"
#include "pixelworld/physics/PixelWorldPhysicsModule.h"
#include "scene/physics/ScenePhysics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>

TEST_CASE("physics.consumer.sceneRuntimeBindsGeneratedCollidersToGameplayWorld") {
    auto& provider = eve::physics::physicsArtifactProvider();
    provider.clear();
    eve::physics::World3D world(0.f, -9.8f, 0.f, true);
    eve::scene_physics::ScenePhysics runtime;

    runtime.bindGeneratedColliders(world).expect("bind generated collider world");
    CHECK_EQ(runtime.boundWorld(), world.runtimeHandle());
    CHECK_EQ(provider.boundWorld(), world.runtimeHandle());
    runtime.unbindGeneratedColliders().expect("unbind generated collider world");
    CHECK(runtime.boundWorld().isInvalid());
}

TEST_CASE("physics.consumer.sceneRuntimeObservesDestroyedGameplayWorldAsStale") {
    auto& provider = eve::physics::physicsArtifactProvider();
    provider.clear();
    eve::scene_physics::ScenePhysics runtime;
    {
        auto world = std::make_unique<eve::physics::World3D>(0.f, 0.f, 0.f, true);
        runtime.bindGeneratedColliders(*world).expect("bind temporary world");
        CHECK(!runtime.boundWorld().isInvalid());
    }
    CHECK(runtime.boundWorld().isInvalid());
    provider.clear();
}

TEST_CASE("pixelworld_physics.moduleCreatesProductionTerrainProjection") {
    eve::pixelworld_physics::PixelWorldPhysics module;
    std::unique_ptr<eve::pixelworld_physics::PixelTerrainCollisionCache> cache(module.newTerrainCache());
    REQUIRE(cache != nullptr);
    eve::pixelworld::PixelWorld pixels(7001);
    for (int x = 0; x < 16; ++x) pixels.setMaterial(x, 12, "stone");
    eve::physics::World world(0.f, 100.f, true, 64.f);

    const auto receipt = cache->sync(world, pixels).expect("sync script-owned terrain cache");
    CHECK_GT(receipt.fixturesCreated, std::uint32_t(0));
    CHECK_EQ(cache->sourceRevision(), pixels.revision());
    CHECK_GT(cache->bodyCount(), std::size_t(0));
    cache->clearPhysics(world).expect("clear script-owned terrain cache");
}
