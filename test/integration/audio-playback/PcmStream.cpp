// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "platform-audio",
  "cases": {
    "script-binding": [
      "audio.pcm.scriptResultAndOwnership"
    ],
    "platform-audio": [
      "audio.pcm.validationAndBackpressureAreAtomic",
      "audio.pcm.starvationPauseResumeAndDrain",
      "audio.pcm.bothDestructionOrders"
    ]
  },
  "module": "audio"
}
*/
// clang-format on
#include "audio/PcmStream.h"
#include "audio/Audio.h"
#include "common/Module.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <thread>
#include <vector>

using eve::audio::Audio;
using eve::audio::PcmStream;
using eve::audio::PcmStreamPhase;

TEST_CASE("audio.pcm.validationAndBackpressureAreAtomic") {
    Audio audio;
    auto  badRate = audio.newPcmStream(0, 1);
    REQUIRE(!badRate.ok());
    auto badChannels = audio.newPcmStream(48000, 3);
    REQUIRE(!badChannels.ok());
    auto created = audio.newPcmStream(48000, 2);
    REQUIRE(created.ok());
    auto stream = std::move(created).takeValue();
    for (const auto& invalid : std::vector<std::vector<float>>{{},
                                                               {0.f},
                                                               {0.f, 2.f},
                                                               {0.f, std::numeric_limits<float>::quiet_NaN()},
                                                               {0.f, std::numeric_limits<float>::infinity()},
                                                               std::vector<float>(8194, 0.f)}) {
        auto rejected = stream->submit(invalid);
        REQUIRE(!rejected.ok());
        REQUIRE(stream->getInfo().queuedFrames == 0);
    }
    std::vector<float> samples(128, .25f);
    for (int i = 0; i < PcmStream::blockCount; ++i) {
        auto queued = stream->submit(samples);
        REQUIRE(queued.ok());
        REQUIRE(queued.value() == 64);
    }
    samples.assign(128, -1.f);  // queued blocks own their input
    auto full = stream->submit(samples);
    REQUIRE(!full.ok());
    REQUIRE(full.error()->code() == eve::DiagnosticCode::Conflict);
    REQUIRE(stream->getInfo().queuedFrames == 512);
    REQUIRE(stream->reset().ok());
    REQUIRE(stream->getInfo().availableBlocks == PcmStream::blockCount);
    REQUIRE(stream->submit(samples).ok());
    REQUIRE(stream->finishInput().ok());
    REQUIRE(stream->finishInput().ok());
    auto ended = stream->submit(samples);
    REQUIRE(!ended.ok());
    REQUIRE(stream->getInfo().queuedFrames == 64);
}

TEST_CASE("audio.pcm.starvationPauseResumeAndDrain") {
    Audio audio;
    auto  created = audio.newPcmStream(48000, 1);
    REQUIRE(created.ok());
    auto stream = std::move(created).takeValue();
    REQUIRE(stream->play().ok());
    REQUIRE(stream->getInfo().phase == PcmStreamPhase::Starved);
    REQUIRE(stream->pause().ok());
    const std::vector<float> samples(480, 0.f);
    REQUIRE(stream->submit(samples).ok());
    audio.pump();
    REQUIRE(stream->getInfo().phase == PcmStreamPhase::Paused);
    REQUIRE(stream->getInfo().playedFrames == 0);
    REQUIRE(stream->play().ok());
    auto waitFor = [&](PcmStreamPhase phase) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (stream->getInfo().phase != phase && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            audio.pump();
        }
        REQUIRE(stream->getInfo().phase == phase);
    };
    waitFor(PcmStreamPhase::Starved);
    REQUIRE(stream->getInfo().playedFrames == 480);
    REQUIRE(stream->submit(samples).ok());  // auto resumes the same source
    REQUIRE(stream->finishInput().ok());
    waitFor(PcmStreamPhase::Finished);
    REQUIRE(stream->getInfo().playedFrames == 960);
    REQUIRE(stream->getInfo().queuedFrames == 0);
    REQUIRE(stream->reset().ok());
    REQUIRE(stream->getInfo().phase == PcmStreamPhase::Idle);
    REQUIRE(stream->getInfo().playedFrames == 0);
    stream->close();
    stream->close();
    REQUIRE(stream->getInfo().phase == PcmStreamPhase::Closed);
    REQUIRE(!stream->play().ok());
    REQUIRE(!stream->submit(samples).ok());
}

TEST_CASE("audio.pcm.bothDestructionOrders") {
    auto audio   = std::make_unique<Audio>();
    auto created = audio->newPcmStream(48000, 1);
    REQUIRE(created.ok());
    auto stream = std::move(created).takeValue();
    {
        auto temporary = audio->newPcmStream(44100, 2);
        REQUIRE(temporary.ok());
    }
    audio->pump();  // expired backend weak reference is removed
    audio.reset();
    REQUIRE(stream->getInfo().phase == PcmStreamPhase::Closed);
    REQUIRE(!stream->reset().ok());
    stream.reset();  // no OpenAL call after the device/context has died
}

TEST_CASE("audio.pcm.scriptResultAndOwnership") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local audio = eve.Audio();
        local invalid = audio.newPcmStream(48000, 4);
        assert(!invalid.ok && !invalid.hasValue);
        local created = audio.newPcmStream(48000, 1);
        assert(created.ok && created.hasValue);
        local stream = created.value;
        assert(!stream.submit(["wrong"]).ok);
        assert(!stream.submit([2.0]).ok);
        local input = [0.0, 0.25, -0.25, 0.0];
        assert(stream.submit(input).value == 4);
        input.clear();
        assert(stream.getInfo().queuedFrames == 4);
        assert(stream.pause().ok);
        assert(stream.finishInput().ok);
        assert(!stream.submit([0.0]).ok);
        assert(stream.reset().ok);
        assert(stream.play().ok);
        assert(stream.getInfo().phase == "starved");
        stream.close();
        assert(!stream.play().ok);
        assert(stream.getInfo().phase == "closed");
        pcmScriptPassed <- true;
    )"));
    REQUIRE(vm.find("pcmScriptPassed").toBool());
}
