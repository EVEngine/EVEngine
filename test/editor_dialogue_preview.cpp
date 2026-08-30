#include "editor/EditorDialoguePreview.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.dialogue.subtitle_preview_checks_safe_area_readability_and_lip_envelope") {
    EditorAudioPcm pcm{4, 1, {0.0F, 0.5F, 1.0F, 0.5F}};
    AudioWaveformService waveformService;
    const auto waveform = waveformService.generate({"voice", 9, 4, 0.0, 0.0}, pcm);
    DialogueSubtitleRequest request;
    request.lineId = "line-1"; request.locale = "en"; request.speaker = "Ada";
    request.text = "This sentence wraps into a deterministic subtitle preview.";
    request.sourceRevision = 9; request.viewportWidth = 1920.0; request.viewportHeight = 1080.0;
    request.maximumCharactersPerLine = 24; request.maximumLines = 3; request.voiceDuration = 1.0;
    DialogueSubtitlePreviewService service;
    const auto preview = service.build(request, &waveform);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.safeX, 96.0); CHECK_EQ(preview.safeWidth, 1728.0);
    CHECK(preview.lines.size() > 1U); CHECK_EQ(preview.lipEnvelope.size(), 4U);
    REQUIRE(!preview.diagnostics.empty());

    request.maximumLines = 1;
    CHECK_EQ(static_cast<int>(service.build(request).status), static_cast<int>(EditorStatus::Failed));
    request.maximumLines = 3; request.sourceRevision = 10;
    const auto stale = service.build(request, &waveform);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Applied));
    CHECK(stale.lipEnvelope.empty());
}
