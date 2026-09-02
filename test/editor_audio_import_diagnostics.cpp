#include "audio_editor/EditorAudioImportDiagnostics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

using namespace eve::editor;

TEST_CASE("editor.audio.import_diagnostics_detect_clip_dc_and_streaming_budget") {
    EditorAudioPcm pcm; pcm.sampleRate=10; pcm.channels=2;
    pcm.samples={1.0F,0.2F, 1.0F,0.2F, 0.5F,0.2F, 0.5F,0.2F};
    AudioImportInspectionRequest request;request.asset="voice";request.sourceRevision=3;request.codec="wav";
    request.staticMemoryBudget=8;
    AudioImportDiagnosticsService service;auto result=service.inspect(request,pcm);
    CHECK_EQ(static_cast<int>(result.status),static_cast<int>(EditorStatus::Rejected));

    pcm.sampleRate=8000;result=service.inspect(request,pcm);
    CHECK_EQ(static_cast<int>(result.status),static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(result.channels.size(),2U);CHECK_EQ(result.channels[0].clippedSamples,2U);
    CHECK(result.channels[1].dcOffset>0.19);CHECK(result.recommendStreaming);
    CHECK(result.diagnostics.size()>=3U);
}

TEST_CASE("editor.audio.import_diagnostics_rejects_nonfinite_and_reports_silence") {
    AudioImportInspectionRequest request;request.asset="quiet";request.sourceRevision=1;request.codec="ogg";
    EditorAudioPcm pcm;pcm.sampleRate=48000;pcm.channels=1;pcm.samples.assign(480,0.0F);
    AudioImportDiagnosticsService service;auto quiet=service.inspect(request,pcm);
    CHECK_EQ(static_cast<int>(quiet.status),static_cast<int>(EditorStatus::Applied));
    CHECK(!quiet.diagnostics.empty());
    pcm.samples[4]=std::numeric_limits<float>::infinity();
    CHECK_EQ(static_cast<int>(service.inspect(request,pcm).status),static_cast<int>(EditorStatus::Rejected));
}
