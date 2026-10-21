#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include "particles/ParticleAppearance.h"
#include "particles/ParticleRecording.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/particle_capture/ParticleBakeCapture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
struct ReferenceParticle {
    std::uint64_t                      serial;
    eve::particles::ParticleAppearance pose;
};
struct ReferenceFrame {
    float                          seconds;
    std::vector<ReferenceParticle> particles;
};
}  // namespace
TEST_CASE("physics.bake.particleReplayMatchesLiveAppearanceAndLifetime") {
    using namespace eve::particles;
    using namespace eve::physics;
    constexpr float dt = 1.f / 64.f;  // Exact binary time isolates lifetime semantics from timestamp rounding.
    std::vector<ReferenceFrame> reference;
    auto*                       emitter = ParticleEmitter::createEmitter(8);
    REQUIRE(emitter != nullptr);
    emitter->setRandomSeed(314);
    auto config          = emitter->config();
    config->emissionRate = 0;
    config->lifeMin = config->lifeMax = .2f;
    config->speedMin                  = 10;
    config->speedMax                  = 20;
    config->gravityY                  = 30;
    config->particleW                 = 4;
    config->particleH                 = 2;
    config->sizeStart                 = 1;
    config->sizeEnd                   = 3;
    config->startRotMin               = 170;
    config->startRotMax               = 190;
    config->spinMin = config->spinMax = 12;
    config->colorStart                = eve::Color(1, .25f, 0, 1);
    config->colorEnd                  = eve::Color(0, .75f, 1, 0);
    config->hframes                   = 4;
    config->frameRate                 = 16;
    prepareParticleRecording(*emitter, 4096).expect("prepare real producer");
    for (int tick = 0; tick < 64; ++tick) {
        if (tick % 8 == 0) emitter->emit(1);
        auto           sim = emitter->sim();
        ReferenceFrame frame{static_cast<float>(sim->recordingSeconds), {}};
        for (int index = 0; index < sim->alive; ++index) {
            const auto& particle = sim->particles[static_cast<std::size_t>(index)];
            frame.particles.push_back(
                {particle.birthSerial, evaluateParticleAppearance(*config, particle).expect("live appearance")});
        }
        reference.push_back(std::move(frame));
        REQUIRE(advanceEmitterSim(*config, *sim, dt) > 0);
    }
    auto events = finishParticleRecording(*emitter).expect("finish producer");
    ecs::DestroyEntity(emitter);
    REQUIRE(reference[0].particles.size() == 1);
    REQUIRE(reference[4].particles[0].serial == reference[0].particles[0].serial);
    REQUIRE(reference[4].particles[0].pose.w > reference[0].particles[0].pose.w);
    REQUIRE(reference[4].particles[0].pose.color.a < reference[0].particles[0].pose.color.a);
    REQUIRE(std::abs(reference[4].particles[0].pose.rotation - reference[0].particles[0].pose.rotation) > 1);
    BakeRecipe recipe;
    recipe.setProducer("particle-reference-v1").expect("producer");
    recipe.setBackend("cpu").expect("backend");
    recipe.setSource("fixture", "eight-overlapping-births-v1").expect("source");
    recipe.setParameters("gravity=30;life=.2;atlas=4;frameRate=16").expect("parameters");
    recipe.setRandomStream("particles", 314).expect("seed");
    recipe.setTiming(dt, 1).expect("timing");
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    eve::PersistentId::Bytes bytes{};
    bytes.back() = 3;
    const eve::PersistentId id(bytes);
    auto                    track = buildParticleTrack(id, 1.f, events, recipe).expect("independent baked track");
    REQUIRE(track.capacity() == 8);
    std::vector<BakedParticleSample> output(track.capacity());
    for (const auto& frame : reference) {
        const auto count = track.sample(frame.seconds, output).expect("replay reference time");
        REQUIRE(count == frame.particles.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& sample = output[index];
            REQUIRE(sample.emitter == id);
            const auto found = std::find_if(frame.particles.begin(), frame.particles.end(),
                                            [&](const auto& value) { return value.serial == sample.birthSerial; });
            REQUIRE(found != frame.particles.end());
            const auto&      a = sample.pose;
            const auto&      b = found->pose;
            const std::array actual{a.x, a.y, a.w, a.h, a.rotation, a.r, a.g, a.b, a.a, a.u0, a.v0, a.u1, a.v1};
            const std::array expected{b.x,       b.y,       b.w,  b.h,  b.rotation, b.color.r, b.color.g,
                                      b.color.b, b.color.a, b.u0, b.v0, b.u1,       b.v1};
            for (std::size_t field = 0; field < actual.size(); ++field)
                REQUIRE(std::abs(actual[field] - expected[field]) < 1e-5f);
        }
    }
    int deaths = 0;
    for (const auto& event : events) {
        if (event.kind != ParticleRecordEventKind::Death) continue;
        ++deaths;
        const float death = static_cast<float>(event.seconds);
        auto        count = track.sample(death, output).expect("exact death");
        REQUIRE(std::none_of(output.begin(), output.begin() + count,
                             [&](const auto& sample) { return sample.birthSerial == event.birthSerial; }));
        count =
            track.sample(std::nextafter(death, -std::numeric_limits<float>::infinity()), output).expect("before death");
        REQUIRE(std::any_of(output.begin(), output.begin() + count,
                            [&](const auto& sample) { return sample.birthSerial == event.birthSerial; }));
    }
    REQUIRE(deaths == 8);
    REQUIRE(track.sample(1.f, output).expect("terminal frame") == 0);
}
