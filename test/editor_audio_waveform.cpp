#include "audio_editor/EditorAudioWaveform.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics") {
    EditorAudioPcm pcm;
    pcm.sampleRate = 4; pcm.channels = 1; pcm.samples = {-1.0F, -0.5F, 0.5F, 1.0F,
                                                         0.8F, 0.4F, -0.4F, -0.8F};
    AudioWaveformService service;
    const auto waveform = service.generate({"asset://sound", 7, 4, 0.25, 1.75}, pcm);
    CHECK_EQ(static_cast<int>(waveform.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(waveform.sourceRevision, 7U); CHECK_EQ(waveform.duration, 2.0);
    REQUIRE_EQ(waveform.envelopes.size(), 1U); CHECK_EQ(waveform.envelopes[0].size(), 4U);
    CHECK(waveform.loopStartBucket >= 0); CHECK(waveform.loopEndBucket >= waveform.loopStartBucket);
    CHECK(waveform.loopSeamDelta > 0.1); REQUIRE(!waveform.diagnostics.empty());
    CHECK_EQ(static_cast<int>(service.generate({"asset://sound", 7, 0, 0.0, 0.0}, pcm).status),
             static_cast<int>(EditorStatus::Rejected));
}
