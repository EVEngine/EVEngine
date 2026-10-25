// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "physics_destruction",
  "origins": [
    "physics_collapse_animation.cpp"
  ]
}
*/
// clang-format on

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include "physics/World3D.h"
#include "physics/destruction/CollapseAnimation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
TEST_CASE("physics_collapse_animation.savedPlaybackOutlivesWorld") {
    CollapseAnimation clip;
    {
        World3D world(0, -9.8f, 0, true);
        auto    asset    = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f).expect("fixture");
        auto    instance = GeometryCollectionInstance::create(world, asset, 0, 4, 0).expect("instance");
        REQUIRE(clip.capture(*instance, 0).ok());
        REQUIRE(!clip.capture(*instance, 0).ok());
        REQUIRE(clip.frameCount() == 1);
        DestructionField field;
        field.kind      = DestructionFieldKind::Strain;
        field.radius    = 100;
        field.magnitude = 100;
        REQUIRE(instance->applyField(field).ok());
        auto delta = eve::Duration::fromSeconds(1.0 / 60).expect("dt");
        REQUIRE(instance->step({eve::SimulationTick{1}, delta}).ok());
        for (int i = 0; i < 30; ++i) world.update(1.f / 60);
        REQUIRE(clip.capture(*instance, 0.5f).ok());
    }
    const auto path = std::filesystem::temp_directory_path() / "eve-collapse-roundtrip.evecollapse";
    std::filesystem::remove(path);
    REQUIRE(clip.save(path.string()).ok());
    REQUIRE(!clip.save(path.string()).ok());
    auto loaded = CollapseAnimation::load(path.string()).expect("reload");
    REQUIRE(loaded.frameCount() == 2);
    REQUIRE(loaded.boneCount() == 2);
    const auto first = loaded.sample(-1, 0).expect("first");
    const auto last  = loaded.sample(99, 0).expect("last");
    const auto mid   = loaded.sample(.25f, 0).expect("mid");
    REQUIRE(last.worldY < first.worldY - .1f);
    REQUIRE(std::abs(mid.worldY - (first.worldY + last.worldY) * .5f) < .0001f);
    REQUIRE(std::abs(last.worldY - clip.sample(.5f, 0).expect("original").worldY) < .0001f);
    REQUIRE(!loaded.sample(std::numeric_limits<float>::quiet_NaN(), 0).ok());
    REQUIRE(!loaded.sample(0, -1).ok());
    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.put('x');
    }
    REQUIRE(!CollapseAnimation::load(path.string()).ok());
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write("EVECOLL1", 8);
    }
    REQUIRE(!CollapseAnimation::load(path.string()).ok());
    std::filesystem::remove(path);
}
TEST_CASE("physics_collapse_animation.emptyAndStaleRecording") {
    CollapseAnimation clip;
    REQUIRE(!clip.sample(0, 0).ok());
    REQUIRE(!clip.save("").ok());
    auto world    = std::make_unique<World3D>(0.f, -9.8f, 0.f, true);
    auto asset    = GeometryCollectionAsset::makeWeldedBoxesFixture().expect("fixture");
    auto instance = GeometryCollectionInstance::create(*world, asset, 0, 0, 0).expect("instance");
    REQUIRE(!clip.capture(*instance, -1).ok());
    REQUIRE(clip.capture(*instance, 0).ok());
    world.reset();
    REQUIRE(!clip.capture(*instance, 1).ok());
    REQUIRE(clip.frameCount() == 1);
    REQUIRE(clip.sample(0, 0).ok());
}

TEST_CASE("physics_collapse_animation.contentKeyIncludesEveryByte") {
    const auto key = CollapseAnimation::contentKey("house recipe");
    REQUIRE(key.size() == 32);
    REQUIRE(key == CollapseAnimation::contentKey("house recipe"));
    REQUIRE(key != CollapseAnimation::contentKey("House recipe"));
    REQUIRE(key != CollapseAnimation::contentKey("house recipE"));
    REQUIRE(CollapseAnimation::contentKey(std::string_view("a\0b", 3)) != CollapseAnimation::contentKey("a"));
    REQUIRE(CollapseAnimation::contentKey("").size() == 32);
}
