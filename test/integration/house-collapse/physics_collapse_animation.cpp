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
#include <simplesquirrel/simplesquirrel.hpp>
#include "physics/World3D.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/CollapseBakeRecipe.h"
#include "physics/destruction/CollapsePlayback.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
TEST_CASE("physics_collapse_animation.savedPlaybackOutlivesWorld") {
    CollapseBakeRecipe recipe;
    recipe.setProducer("native-collapse-test-v1").expect("producer");
    recipe.setBackend("box3d-test-v1").expect("backend");
    recipe.setParameters("gravity=-9.8;strain=100").expect("parameters");
    recipe.setSource("fixture", "welded-boxes-v1").expect("source");
    recipe.setTiming(1.f / 60, 30).expect("timing");
    CollapseAnimation clip;
    clip.beginBake(recipe).expect("identify bake");
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
    auto loaded = CollapseAnimation::loadForRecipe(path.string(), recipe).expect("identified reload");
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

TEST_CASE("physics_collapse_animation.scriptPlayerOwnsFrozenClip") {
    CollapseAnimation clip;
    {
        World3D world(0, 0, 0, true);
        auto    asset    = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f).expect("fixture");
        auto    instance = GeometryCollectionInstance::create(world, asset, 0, 0, 0).expect("instance");
        clip.capture(*instance, 0).expect("first frame");
        clip.capture(*instance, 1).expect("last frame");
    }
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeCollapseAnimation(table);
    vm.addFunc("source", [&] { return &clip; });
    vm.addFunc("replaceSource", [&] { clip = CollapseAnimation{}; });
    vm.addFunc("sampleX",
               [](CollapsePlayback* player) { return player->sample(0).expect("script player sample").worldX; });
    vm.run(vm.compileSource(R"(
        local recipe = eve.CollapseBakeRecipe();
        assert(!recipe.key().ok);
        assert(recipe.setProducer("script-test-v1").ok);
        assert(recipe.setBackend("box3d-test-v1").ok);
        assert(recipe.setParameters("parameters").ok);
        assert(recipe.setSource("asset", "bytes").ok);
        assert(recipe.setTiming(0.5, 2).ok);
        assert(recipe.setRandomStream("fracture", 42).ok);
        assert(recipe.key().ok);
        assert(!recipe.setTiming(0.0, 1).ok);
        assert(!recipe.setRandomStream("fracture", -1).ok);
        local recording = eve.CollapseAnimation();
        assert(recording.beginBake(recipe).ok);
        assert(!recording.beginBake(null).ok);
        assert(!recording.beginBake(source()).ok);
        assert(!recording.loadForRecipe("", null).ok);
        assert(!source().loadForRecipe("", recipe).ok);
        assert(source().duration() == 1.0);
        local made = source().createPlayback();
        assert(made.ok && made.hasValue);
        local player = made.value;
        local copied = player.copy();
        assert(copied.ok && copied.hasValue);
        local other = copied.value;
        replaceSource();
        assert(!source().createPlayback().ok);
        assert(player.advance(0.25).ok);
        assert(player.time() == 0.25 && other.time() == 0.0);
        player.setPaused(true);
        assert(player.advance(2.0).ok && player.time() == 0.25);
        assert(player.seek(0.75).ok);
        assert(player.setRate(-2.0).ok && player.rate() == -2.0);
        assert(player.setLoop(true).ok);
        player.setPaused(false);
        assert(player.advance(0.5).ok && player.time() == 0.75);
        assert(player.duration() == 1.0 && !player.paused());
        local before = sampleX(player);
        assert(player.setPlacement(5.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
        assert(sampleX(player) == before + 5.0);
        assert(sampleX(other) == before);
        assert(!player.setPlacement(0.0,0.0,0.0,0.0,0.0,0.0,0.0).ok);
        assert(!player.advance(-0.5).ok);
        assert(sampleX(player) == before + 5.0 && player.time() == 0.75);
    )"));
}
