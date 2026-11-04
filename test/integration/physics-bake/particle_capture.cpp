// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "module-composition",
  "module": "particles"
}
*/
// clang-format on

#include "particles/ParticleRecording.h"
#include "particles/bake/capture/ParticleBakeCapture.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
using eve::particles::buildParticleTrack;
using eve::particles::ParticleRecordingSource;
TEST_CASE("physics.bake.particleEventsBecomeIndependentTrajectories") {
    using namespace eve::particles;
    auto* emitter = ParticleEmitter::createEmitter(4);
    emitter->setRandomSeed(8);
    emitter->config()->lifeMin = emitter->config()->lifeMax = 10;
    REQUIRE(prepareParticleRecording(*emitter, 32).ok());
    emitter->emit(1);
    REQUIRE(advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.25f) > 0);
    auto events = finishParticleRecording(*emitter).expect("finish");
    ecs::DestroyEntity(emitter);
    BakeRecipe recipe;
    recipe.setProducer("particle-capture-test").expect("producer");
    recipe.setBackend("cpu").expect("backend");
    recipe.setSource("emitter", "single").expect("source");
    recipe.setParameters("").expect("parameters");
    recipe.setRandomStream("particles", 8).expect("seed");
    recipe.setTiming(0.25f, 1).expect("timing");
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    eve::PersistentId::Bytes bytes{};
    bytes.back() = 1;
    const eve::PersistentId          id(bytes);
    auto                             track = buildParticleTrack(id, 0.25f, events, recipe).expect("track");
    std::vector<BakedParticleSample> output(track.capacity());
    REQUIRE(track.sample(0.125f, output).value() == 1);
    REQUIRE(output[0].birthSerial == 1);
    REQUIRE(track.sample(0.25f, output).value() == 0);
    auto noDeath = events;
    noDeath.pop_back();
    REQUIRE(!buildParticleTrack(id, 0.25f, noDeath, recipe).ok());
    auto afterDeath = events;
    afterDeath.push_back(events.back());
    REQUIRE(!buildParticleTrack(id, 0.25f, afterDeath, recipe).ok());
    auto secondBytes   = bytes;
    secondBytes.back() = 2;
    const eve::PersistentId          secondId(secondBytes);
    const ParticleRecordingSource    sources[]{{id, events}, {secondId, events}};
    auto                             combined = buildParticleTrack(0.25f, sources, recipe).expect("combined");
    std::vector<BakedParticleSample> both(combined.capacity());
    REQUIRE(combined.sample(0.125f, both).value() == 2);
    REQUIRE(both[0].emitter == id);
    REQUIRE(both[1].emitter == secondId);
    REQUIRE(both[0].birthSerial == both[1].birthSerial);
    const ParticleRecordingSource duplicate[]{{id, events}, {id, events}};
    REQUIRE(!buildParticleTrack(0.25f, duplicate, recipe).ok());
    events.clear();
    REQUIRE(track.sample(0.125f, output).value() == 1);
}
