#include <filesystem>
#include <limits>
#include <random>
#include <simplesquirrel/simplesquirrel.hpp>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/DeformTrack.h"
#include "physics/backend/bake/ParticleTrack.h"
#include "physics/backend/bake/RigidPoseTrack.h"
#include "physics/backend/bake/script/PhysicsBake.h"
#include "physics/destruction/CollapseBakeRecipe.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
TEST_CASE("physics.bake.scriptRigidPlaybackOwnsLoadedTrackAndCoexistsWithLegacyRecipe") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("eve-bake-script-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    const auto file = (dir / "rigid.bake").string();
    BakeRecipe recipe;
    recipe.setProducer("script-rigid-v1").expect("producer");
    recipe.setBackend("fixture-v1").expect("backend");
    recipe.setParameters("").expect("parameters");
    recipe.setSource("fixture", "poses").expect("source");
    recipe.setTiming(1, 1).expect("timing");
    recipe.setRandomStream("test", std::numeric_limits<std::uint64_t>::max()).expect("seed");
    eve::PersistentId::Bytes bytes{};
    bytes.back() = 1;
    const eve::PersistentId id(bytes);
    {
        auto track =
            RigidPoseTrack::prepare(RigidTrackSpace::Metres3D,
                                    {{0, {{id, {0, 0, 0, 0, 0, 0, 1}}}}, {1, {{id, {10, 0, 0, 0, 0, 0, 1}}}}}, recipe)
                .expect("prepare");
        track.save(file).expect("save");
    }
    // Both registration orders must keep native type ownership and methods independent.
    for (bool legacyFirst : {false, true}) {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        if (legacyFirst) exposeCollapseBakeRecipe(table);
        PhysicsBake::expose(table);
        if (!legacyFirst) exposeCollapseBakeRecipe(table);
        vm.addFunc("assetPath", [&] { return file; });
        vm.addFunc("releaseAsset", [&] { return !legacyFirst || std::filesystem::remove(file); });
        vm.addFunc("expectedKey", [&] { return recipe.key().expect("key"); });
        vm.run(vm.compileSource(R"(
            local recipe = eve.BakeRecipe();
            assert(!recipe.key().ok);
            assert(recipe.setCoordinates("metres3d").ok);
            assert(!recipe.setCoordinates("unknown").ok);
            assert(recipe.setProducer("script-rigid-v1").ok);
            assert(recipe.setBackend("fixture-v1").ok);
            assert(recipe.setParameters("").ok);
            assert(recipe.setSource("fixture", "poses").ok);
            assert(recipe.setTiming(1.0, 1).ok);
            assert(recipe.setRandomStream("test", "18446744073709551615").ok);
            assert(recipe.key().value == expectedKey());
            foreach (seed in ["", "-1", "+1", "1x", "18446744073709551616"]) {
                assert(!recipe.setRandomStream("test", seed).ok);
                assert(recipe.key().value == expectedKey());
            }
            local legacy = eve.CollapseBakeRecipe();
            assert(recipe.producerBuildRevision().len() == 64);
            assert(recipe.producerBuildRevision() == legacy.producerBuildRevision());
            assert(recipe.box3DBackendRevision() == legacy.box3DBackendRevision());
            assert(legacy.setProducer("legacy").ok);
            assert(!legacy.key().ok);
            local bake = eve.PhysicsBake();
            assert(!bake.loadRigid(assetPath(), null).ok);
            assert(!bake.loadRigid(assetPath(), legacy).ok);
            local result = bake.loadRigid(assetPath(), recipe);
            assert(result.ok && result.hasValue);
            local player = result.value;
            assert(player.trackCount() == 1);
            assert(!player.trackId(-1).ok && !player.trackId(1).ok);
            local id = player.trackId(0).value;
            assert(!player.sample("invalid").ok);
            local other = player.copy().value;
            assert(player.seek(0.5).ok);
            assert(player.sample(id).value.x == 5.0);
            assert(other.sample(id).value.x == 0.0);
            assert(player.setPlacement(2.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
            assert(player.sample(id).value.x == 7.0);
            assert(player.setRate(-1.0).ok && player.setLoop(true).ok);
            assert(player.advance(0.75).ok && player.time() == 0.75);
            player.setPaused(true);
            assert(player.advance(1.0).ok && player.time() == 0.75);
            assert(!player.advance(-1.0).ok && player.time() == 0.75);
            assert(recipe.setSource("fixture", "different").ok);
            assert(!bake.loadRigid(assetPath(), recipe).ok);
            assert(releaseAsset());
            recipe = null; result = null; player = null;
            collectgarbage();
            assert(other.seek(1.0).ok && other.sample(id).value.x == 10.0);
        )"));
    }
}

TEST_CASE("physics.bake.scriptDeformSamplesOwnTopologyAndPositions") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("eve-deform-script-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    for (bool planar : {false, true}) {
        BakeRecipe recipe;
        recipe.setCoordinates(planar ? BakeCoordinates::PlanarPixels : BakeCoordinates::MetresRightHandedYUp)
            .expect("coordinates");
        recipe.setProducer("deform-script-v1").expect("producer");
        recipe.setBackend("fixture-v1").expect("backend");
        recipe.setParameters("").expect("parameters");
        recipe.setSource("fixture", "vertices").expect("source");
        recipe.setTiming(1, 1).expect("timing");
        const auto path = (dir / (planar ? "rope.bake" : "cloth.bake")).string();
        {
            auto topology = DeformTopology::prepare(
                                planar ? DeformPrimitive::Segments : DeformPrimitive::Triangles, 3,
                                planar ? std::vector<std::uint32_t>{0, 1, 1, 2} : std::vector<std::uint32_t>{0, 1, 2},
                                planar ? std::vector<DeformUV>{} : std::vector<DeformUV>{{0, 0}, {1, 0}, {0, 1}})
                                .expect("topology");
            auto track = DeformTrack::prepare({{0, topology, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}},
                                               {1, topology, {{10, 0, 0}, {11, 0, 0}, {10, 1, 0}}}},
                                              recipe)
                             .expect("track");
            track.save(path).expect("save");
        }
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        PhysicsBake::expose(table);
        vm.addFunc("assetPath", [&] { return path; });
        vm.addFunc("planar", [&] { return planar; });
        vm.addFunc("releaseAsset", [&] { return std::filesystem::remove(path); });
        vm.run(vm.compileSource(R"(
            local recipe = eve.BakeRecipe();
            assert(recipe.setCoordinates(planar() ? "pixels2d" : "metres3d").ok);
            assert(recipe.setProducer("deform-script-v1").ok);
            assert(recipe.setBackend("fixture-v1").ok);
            assert(recipe.setParameters("").ok);
            assert(recipe.setSource("fixture", "vertices").ok);
            assert(recipe.setTiming(1.0,1).ok);
            local bake = eve.PhysicsBake();
            assert(!bake.loadDeform(assetPath(), null).ok);
            assert(!bake.loadRigid(assetPath(), recipe).ok);
            local result = bake.loadDeform(assetPath(), recipe);
            assert(result.ok && result.hasValue);
            local player = result.value;
            assert(player.seek(0.5).ok);
            local frame = player.captureSample().value;
            assert(frame.vertexCount() == 3 && frame.point(0).value.x == 5.0);
            assert(!frame.point(-1).ok && !frame.point(3).ok);
            assert(frame.index(0).value == 0 && frame.index(1).value == 1);
            assert(!frame.index(-1).ok && !frame.index(frame.indexCount()).ok);
            assert(frame.primitive() == (planar() ? "segments" : "triangles"));
            assert(frame.indexCount() == (planar() ? 4 : 3));
            assert(frame.hasUV() == !planar());
            if (planar()) assert(!frame.uv(0).ok);
            else assert(frame.uv(1).value.u == 1.0);
            local other = player.prepareInstance().value;
            assert(other.time() == 0.0 && other.rate() == 1.0 && !other.paused());
            assert(player.setPlacement(2.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
            assert(player.captureSample().value.point(0).value.x == 7.0);
            assert(frame.point(0).value.x == 5.0);
            if (planar()) assert(!player.setPlacement(2.0,0.0,1.0,0.0,0.0,0.0,1.0).ok);
            assert(other.captureSample().value.point(0).value.x == 0.0);
            assert(player.setRate(-1.0).ok && player.setLoop(true).ok);
            assert(player.advance(0.75).ok && player.time() == 0.75);
            player.setPaused(true);
            assert(player.advance(1.0).ok && player.time() == 0.75);
            assert(releaseAsset());
            assert(!bake.loadDeform(assetPath(), recipe).ok);
            player = null; result = null; recipe = null;
            collectgarbage();
            assert(other.seek(1.0).ok);
            local last = other.captureSample().value;
            other = null; collectgarbage();
            assert(last.point(0).value.x == 10.0);
            assert(last.topologyKey() == frame.topologyKey());
            assert(frame.point(0).value.x == 5.0);
        )"));
    }
}

TEST_CASE("physics.bake.scriptParticleSnapshotsPreserveBirthBoundariesAndIdentity") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("eve-particle-script-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    BakeRecipe recipe;
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    recipe.setProducer("particle-script-v1").expect("producer");
    recipe.setBackend("fixture-v1").expect("backend");
    recipe.setParameters("").expect("parameters");
    recipe.setSource("fixture", "births").expect("source");
    recipe.setTiming(0.25f, 1).expect("timing");
    const auto               path = (dir / "particles.bake").string();
    eve::PersistentId::Bytes bytes{};
    bytes.back() = 1;
    const eve::PersistentId first(bytes);
    bytes.back() = 2;
    const eve::PersistentId second(bytes);
    {
        BakedParticlePose a;
        a.w        = 4;
        a.h        = 6;
        a.rotation = 360;
        a.u1 = a.v1 = 0.5f;
        auto b      = a;
        b.x         = 10;
        b.rotation  = 720;
        b.r         = 0;
        b.u0 = b.v0 = 0.5f;
        b.u1 = b.v1 = 1;
        auto c      = a;
        c.x         = 20;
        auto track =
            ParticleTrack::prepare(1,
                                   {{first, std::numeric_limits<std::uint64_t>::max(), {{0.25f, a}, {0.75f, b}}},
                                    {second, 1, {{0.75f, c}, {1, c}}}},
                                   recipe)
                .expect("track");
        track.save(path).expect("save");
    }
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    PhysicsBake::expose(table);
    vm.addFunc("assetPath", [&] { return path; });
    vm.addFunc("firstId", [&] { return first.format(); });
    vm.addFunc("secondId", [&] { return second.format(); });
    vm.addFunc("releaseAsset", [&] { return std::filesystem::remove(path); });
    vm.run(vm.compileSource(R"(
        local recipe = eve.BakeRecipe();
        assert(recipe.setCoordinates("pixels2d").ok);
        assert(recipe.setProducer("particle-script-v1").ok);
        assert(recipe.setBackend("fixture-v1").ok);
        assert(recipe.setParameters("").ok);
        assert(recipe.setSource("fixture", "births").ok);
        assert(recipe.setTiming(0.25,1).ok);
        local bake = eve.PhysicsBake();
        assert(!bake.loadParticles(assetPath(), null).ok);
        local result = bake.loadParticles(assetPath(), recipe);
        assert(result.ok && result.hasValue);
        local player = result.value;
        local empty = player.captureSample().value;
        assert(empty.count() == 0 && !empty.particle(0).ok);
        assert(player.seek(0.25).ok && player.captureSample().value.count() == 1);
        assert(player.seek(0.5).ok);
        local frame = player.captureSample().value;
        assert(frame.count() == 1 && !frame.particle(-1).ok && !frame.particle(1).ok);
        local particle = frame.particle(0).value;
        assert(particle.emitter == firstId() && particle.birthSerial == "18446744073709551615");
        assert(particle.x == 5.0 && particle.w == 4.0 && particle.h == 6.0);
        assert(particle.rotation == 540.0 && particle.r == 0.5);
        assert(particle.u0 == 0.0 && particle.u1 == 0.5);
        local other = player.prepareInstance().value;
        assert(other.time() == 0.0 && other.captureSample().value.count() == 0);
        assert(player.setPlacement(2.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
        assert(player.captureSample().value.particle(0).value.x == 7.0);
        assert(!player.setPlacement(2.0,0.0,1.0,0.0,0.0,0.0,1.0).ok);
        assert(player.setRate(-1.0).ok && player.setLoop(true).ok);
        assert(player.advance(0.75).ok && player.time() == 0.75);
        local boundary = player.captureSample().value;
        assert(boundary.count() == 1 && boundary.particle(0).value.emitter == secondId());
        assert(boundary.particle(0).value.birthSerial == "1");
        assert(boundary.particle(0).value.x == 22.0);
        player.setPaused(true);
        assert(player.advance(1.0).ok && player.time() == 0.75);
        assert(player.seek(1.0).ok && player.captureSample().value.count() == 0);
        assert(releaseAsset());
        assert(!bake.loadParticles(assetPath(), recipe).ok);
        player = null; result = null; recipe = null; collectgarbage();
        assert(other.seek(0.5).ok && other.captureSample().value.particle(0).value.x == 5.0);
        other = null; collectgarbage();
        assert(frame.particle(0).value.x == 5.0);
        assert(frame.particle(0).value.birthSerial == "18446744073709551615");
        assert(boundary.particle(0).value.x == 22.0);
    )"));
}
