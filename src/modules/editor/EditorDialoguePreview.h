#pragma once

#include "editor/EditorAudioWaveform.h"

#include <string>
#include <vector>

namespace eve::editor {

/** @brief Immutable subtitle preview request for one localized dialogue line. */
struct DialogueSubtitleRequest {
    std::string lineId;
    std::string locale;
    std::string speaker;
    std::string text;
    Revision sourceRevision = 0;
    double viewportWidth = 0.0;
    double viewportHeight = 0.0;
    double safeMarginX = 0.05;
    double safeMarginY = 0.05;
    int maximumCharactersPerLine = 42;
    int maximumLines = 3;
    double minimumDuration = 1.0;
    double charactersPerSecond = 15.0;
    double voiceDuration = 0.0;
};

/** @brief Normalized amplitude point suitable for mouth-shape/lip-sync preview. */
struct DialogueLipEnvelopePoint {
    double time = 0.0;
    double amplitude = 0.0;
};

/** @brief Safe-area subtitle layout and voice/readability diagnostics. */
struct DialogueSubtitlePreview {
    EditorStatus status = EditorStatus::Failed;
    std::string lineId;
    Revision sourceRevision = 0;
    double safeX = 0.0, safeY = 0.0, safeWidth = 0.0, safeHeight = 0.0;
    double estimatedReadDuration = 0.0;
    std::vector<std::string> lines;
    std::vector<DialogueLipEnvelopePoint> lipEnvelope;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Renderer-neutral subtitle layout and waveform-derived lip envelope service. */
class DialogueSubtitlePreviewService {
public:
    /** @brief Build safe-area text layout and optionally derive lip amplitude from a waveform. */
    DialogueSubtitlePreview build(const DialogueSubtitleRequest& request,
                                  const AudioWaveformResult* waveform = nullptr) const;
};

}  // namespace eve::editor
