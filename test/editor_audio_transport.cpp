#include "audio/editor/EditorAudioTransport.h"
#include "audio/editor/EditorAudioWaveform.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_audio_transport.cpp
using namespace eve::editor;

namespace {
class FakeAudioBackend final : public IAudioTransportBackend {
public:
    void play() override {
        isPlaying = true;
        ++plays;
    }
    void pause() override { isPlaying = false; }
    void stop() override {
        isPlaying = false;
        position  = 0.0;
        ++stops;
    }
    Result<void> seek(double seconds) override {
        if (!seekable)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.audio.seek"), "seek rejected");
        position = seconds;
        return eve::editing::applied<void>();
    }
    double tell() const override { return position; }
    double duration() const override { return length; }
    bool   playing() const override { return isPlaying; }
    void   setNativeLooping(bool enabled) override { nativeLoop = enabled; }
    double position   = 0.0;
    double length     = 10.0;
    bool   isPlaying  = false;
    bool   seekable   = true;
    bool   nativeLoop = true;
    int    plays      = 0;
    int    stops      = 0;
};
}  // namespace

TEST_CASE("editor.audio.transport_wraps_custom_loop_and_rejects_stale_revision") {
    FakeAudioBackend       backend;
    AudioAuditionTransport transport;
    CHECK(transport.bind(StableId("voice"), 7, &backend).ok());
    CHECK(!backend.nativeLoop);
    CHECK(transport.setLoop(7, true, 2.0, 4.0).ok());
    CHECK(transport.play(7).ok());
    CHECK_EQ(backend.position, 2.0);
    backend.position = 4.1;
    auto wrapped     = transport.update(7);
    REQUIRE(wrapped.ok());
    CHECK_EQ(wrapped.value().position, 2.0);
    CHECK_EQ(backend.plays, 2);
    CHECK_EQ(static_cast<int>(transport.update(8).code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(backend.stops, 1);
}

TEST_CASE("editor.audio.transport_validates_seek_loop_and_rebinding") {
    FakeAudioBackend       first, second;
    AudioAuditionTransport transport;
    CHECK(transport.bind(StableId("first"), 1, &first).ok());
    CHECK_EQ(static_cast<int>(transport.setLoop(1, true, 5.0, 2.0).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(transport.seek(1, 11.0).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(transport.play(1).ok());
    CHECK(transport.pause(1).ok());
    CHECK(transport.bind(StableId("second"), 2, &second).ok());
    CHECK_EQ(first.stops, 1);
    auto snapshot = transport.snapshot(2);
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().asset.value(), std::string("second"));
}

// editor_audio_waveform.cpp
using namespace eve::editor;

TEST_CASE("editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics") {
    EditorAudioPcm pcm;
    pcm.sampleRate = 4;
    pcm.channels   = 1;
    pcm.samples    = {-1.0F, -0.5F, 0.5F, 1.0F, 0.8F, 0.4F, -0.4F, -0.8F};
    AudioWaveformService service;
    const auto           waveform = service.generate({"asset://sound", 7, 4, 0.25, 1.75}, pcm);
    CHECK_EQ(static_cast<int>(waveform.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(waveform.sourceRevision, 7U);
    CHECK_EQ(waveform.duration, 2.0);
    REQUIRE_EQ(waveform.envelopes.size(), 1U);
    CHECK_EQ(waveform.envelopes[0].size(), 4U);
    CHECK(waveform.loopStartBucket >= 0);
    CHECK(waveform.loopEndBucket >= waveform.loopStartBucket);
    CHECK(waveform.loopSeamDelta > 0.1);
    REQUIRE(!waveform.diagnostics.empty());
    CHECK_EQ(static_cast<int>(service.generate({"asset://sound", 7, 0, 0.0, 0.0}, pcm).status),
             static_cast<int>(EditorStatus::Rejected));
}
