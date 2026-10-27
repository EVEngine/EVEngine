// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "script-binding",
  "module": "particles"
}
*/
// clang-format on

#include <filesystem>
#include <random>
#include <simplesquirrel/simplesquirrel.hpp>
#include <vector>
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "particles/bake/script/ParticleBakeRecording.h"
#include "physics/backend/bake/script/PhysicsBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("physics.bake.scriptParticleJournalsPublishRetryableMultiEmitterArchive") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("eve-particle-record-script-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    ssq::VM vm(2048);
    auto    table = vm.addTable("eve");
    eve::physics::PhysicsBake::expose(table);
    eve::particles::ParticleBakeRecording::expose(table);
    eve::particles::Particles::expose(table);
    vm.addFunc("assetPath", [&] { return (dir / "combined.particles").string(); });
    vm.run(vm.compileSource(R"(
        local recipe = eve.BakeRecipe();
        assert(recipe.setProducer("particle-journal-script-v1").ok);
        assert(recipe.setBackend("cpu-test").ok);
        assert(recipe.setParameters("life=1;speed=0;emission=0").ok);
        assert(recipe.setSource("fixture","two-emitters").ok);
        assert(recipe.setTiming(1.0/60.0,1).ok);
        assert(recipe.setRandomStream("first","7").ok && recipe.setRandomStream("second","9").ok);
        local recorder = eve.Particles().bakeRecording();
        assert(!recorder.newArchive(recipe).ok);
        assert(recipe.setCoordinates("pixels2d").ok);
        local archive = recorder.newArchive(recipe).value;
        assert(!archive.save(assetPath()).ok);
        assert(!recorder.begin(null,128).ok && !recorder.finish(recipe).ok);
        local particles = eve.Particles();
        local one = particles.newEmitter(8);
        local two = particles.newEmitter(8);
        foreach (emitter in [one,two]) {
            emitter.setGpuSimulation(false);
            emitter.setRenderMode("billboard",1.0); emitter.setSortMode("none");
            emitter.setEmissionRate(0.0);
            emitter.setParticleLifetime(1.0,1.0); emitter.setSpeed(0.0,0.0);
        }
        one.setRandomSeed(7); two.setRandomSeed(9);
        assert(!recorder.begin(one,0).ok);
        one.setGpuSimulation(true);
        assert(!recorder.begin(one,128).ok);
        one.setGpuSimulation(false);
        assert(recorder.begin(one,128).ok && recorder.begin(two,128).ok);
        assert(!recorder.begin(one,128).ok);
        one.emit(1); two.emit(1);
        for (local i=0; i<30; ++i) particles.update(1.0/60.0);
        local firstResult = recorder.finish(one);
        local secondResult = recorder.finish(two);
        assert(firstResult.ok && secondResult.ok);
        local first = firstResult.value;
        local second = secondResult.value;
        assert(!recorder.finish(one).ok);
        assert(first.duration() >= 0.5 && first.duration() < 0.500001);
        assert(first.eventCount() >= 32 && second.eventCount() >= 32);
        local id1 = "00000000-0000-0000-0000-000000000001";
        local id2 = "00000000-0000-0000-0000-000000000002";
        assert(!archive.addJournal(null,id1).ok && !archive.addJournal(first,"invalid").ok);
        assert(archive.addJournal(first,id1).ok);
        assert(!archive.addJournal(first,id2).ok);
        assert(!archive.addJournal(second,id1).ok);
        assert(archive.addJournal(second,id2).ok && archive.sourceCount() == 2);
        one.stop(); one.reset(); two.stop(); two.reset();
        one = null; two = null; first = null; second = null;
        firstResult = null; secondResult = null; collectgarbage();
        local failed = particles.newEmitter(1);
        failed.setGpuSimulation(false); failed.setRenderMode("billboard",1.0); failed.setSortMode("none");
        failed.setEmissionRate(0.0); failed.setParticleLifetime(1.0,1.0);
        assert(recorder.begin(failed,1).ok);
        failed.emit(1); particles.update(1.0/60.0);
        assert(!recorder.finish(failed).ok);
        failed.stop(); failed.reset();
        assert(recorder.begin(failed,8).ok);
        local empty = recorder.finish(failed);
        assert(empty.ok && empty.value.eventCount() == 0);
        failed = null; empty = null; collectgarbage();
        assert(!archive.save(assetPath()+"-missing/output").ok);
        assert(archive.sourceCount() == 2);
        assert(recipe.setSource("fixture","changed after archive admission").ok);
        assert(archive.save(assetPath()).ok);
        assert(!archive.save(assetPath()).ok);
        assert(!eve.PhysicsBake().loadParticles(assetPath(),recipe).ok);
        assert(recipe.setSource("fixture","two-emitters").ok);
        local loaded = eve.PhysicsBake().loadParticles(assetPath(),recipe);
        assert(loaded.ok && loaded.hasValue);
        archive = null; recipe = null; collectgarbage();
        local player = loaded.value;
        assert(player.seek(0.25).ok);
        local frame = player.captureSample().value;
        assert(frame.count() == 2);
        assert(frame.particle(0).value.emitter == id1 && frame.particle(1).value.emitter == id2);
        assert(frame.particle(0).value.birthSerial == "1" && frame.particle(1).value.birthSerial == "1");
        assert(player.seek(player.duration()).ok && player.captureSample().value.count() == 0);
    )"));
}

TEST_CASE("physics.bake.scriptParticleAssetOutlivesProducerVmAndEcsSources") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("eve-particle-retirement-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path          path;
        std::vector<ecs::EntityHandle> sources;
        ~Cleanup() {
            for (auto handle : sources)
                if (auto* source = dynamic_cast<eve::particles::ParticleEmitter*>(ecs::try_get(handle)))
                    source->release();
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir, {}};
    const auto  path         = (dir / "retired.particles").string();
    const char* recipeSource = R"(
        recipe <- eve.BakeRecipe();
        assert(recipe.setProducer("particle-retirement-v1").ok);
        assert(recipe.setBackend("cpu").ok);
        assert(recipe.setParameters("life=2;speed=0").ok);
        assert(recipe.setSource("fixture","retired-two-emitters").ok);
        assert(recipe.setCoordinates("pixels2d").ok);
        assert(recipe.setTiming(0.125,1).ok);
        assert(recipe.setRandomStream("particles","11").ok);
    )";
    {
        ssq::VM producer(2048);
        auto    table = producer.addTable("eve");
        eve::physics::PhysicsBake::expose(table);
        eve::particles::ParticleBakeRecording::expose(table);
        eve::particles::Particles::expose(table);
        producer.addFunc("assetPath", [&] { return path; });
        producer.addFunc("rememberSource", [&](eve::particles::ParticleEmitter* emitter) {
            cleanup.sources.push_back(ecs::handle_of(emitter));
        });
        producer.run(producer.compileSource(recipeSource));
        producer.run(producer.compileSource(R"(
            local particles = eve.Particles();
            local recorder = eve.Particles().bakeRecording();
            local archive = recorder.newArchive(recipe).value;
            local sources = [particles.newEmitter(4),particles.newEmitter(4)];
            foreach (i, emitter in sources) {
                rememberSource(emitter);
                emitter.setGpuSimulation(false);
                emitter.setRenderMode("billboard",1.0); emitter.setSortMode("none");
                emitter.setEmissionRate(0.0); emitter.setRandomSeed(11);
                emitter.setPosition(10.0+i*20.0,20.0);
                emitter.setParticleLifetime(2.0,2.0); emitter.setSpeed(0.0,0.0);
                assert(recorder.begin(emitter,64).ok);
                emitter.emit(1);
            }
            for (local tick=0;tick<8;++tick) particles.update(0.125);
            foreach (i, emitter in sources) {
                local journal = recorder.finish(emitter);
                assert(journal.ok);
                local id = i==0 ? "00000000-0000-0000-0000-000000000011"
                               : "00000000-0000-0000-0000-000000000012";
                assert(archive.addJournal(journal.value,id).ok);
            }
            assert(archive.save(assetPath()).ok);
        )"));
    }
    // The producer VM and every script alias are gone before native retirement.
    REQUIRE(cleanup.sources.size() == 2);
    for (auto handle : cleanup.sources) {
        auto* source = dynamic_cast<eve::particles::ParticleEmitter*>(ecs::try_get(handle));
        REQUIRE(source != nullptr);
        source->release();
        REQUIRE(ecs::try_get(handle) == nullptr);
    }
    ssq::VM consumer(2048);
    auto    table = consumer.addTable("eve");
    eve::physics::PhysicsBake::expose(table);
    consumer.addFunc("assetPath", [&] { return path; });
    consumer.run(consumer.compileSource(recipeSource));
    consumer.run(consumer.compileSource(R"(
        assert(!("Particles" in eve) && !("ParticleBakeRecording" in eve));
        local loaded = eve.PhysicsBake().loadParticles(assetPath(),recipe);
        assert(loaded.ok);
        player <- loaded.value;
        other <- player.prepareInstance().value;
        assert(player.seek(0.25).ok && other.seek(0.75).ok);
        assert(other.setPlacement(100.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
        local a = player.captureSample().value;
        local b = other.captureSample().value;
        assert(a.count()==2 && b.count()==2);
        assert(a.particle(0).value.emitter=="00000000-0000-0000-0000-000000000011");
        assert(a.particle(1).value.emitter=="00000000-0000-0000-0000-000000000012");
        assert(a.particle(0).value.birthSerial=="1" && a.particle(1).value.birthSerial=="1");
        assert(b.particle(0).value.x-a.particle(0).value.x==100.0);
        player.setPaused(true);
        assert(player.advance(0.25).ok && player.time()==0.25);
        assert(other.setRate(-1.0).ok && other.advance(0.25).ok && other.time()==0.5);
    )"));
    REQUIRE(std::filesystem::remove(path));
    // Loaded instances own their data even after the backing file is removed.
    consumer.run(consumer.compileSource(R"(
        player=null; recipe=null; collectgarbage();
        assert(other.seek(0.125).ok && other.captureSample().value.count()==2);
        assert(other.seek(other.duration()).ok && other.captureSample().value.count()==0);
    )"));
}
