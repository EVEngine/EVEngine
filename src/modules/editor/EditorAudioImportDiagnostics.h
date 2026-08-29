#pragma once

#include "editor/EditorAudioWaveform.h"

#include <cstddef>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Import metadata and policy supplied before audio publication. */
struct AudioImportInspectionRequest {
    std::string asset;
    Revision sourceRevision = 0;
    std::string codec;
    std::size_t encodedBytes = 0;
    bool streaming = false;
    double clippingThreshold = 0.999;
    double silenceThreshold = 0.0001;
    std::size_t maximumSamples = 100000000;
    std::size_t staticMemoryBudget = 16 * 1024 * 1024;
};

/** @brief Per-channel audio signal statistics. */
struct AudioChannelInspection {
    double peak = 0.0;
    double rms = 0.0;
    double dcOffset = 0.0;
    std::size_t clippedSamples = 0;
};

/** @brief Revision-tagged codec/import QA and memory recommendation. */
struct AudioImportInspectionResult {
    EditorStatus status = EditorStatus::Failed;
    std::string asset;
    Revision sourceRevision = 0;
    double duration = 0.0;
    std::size_t decodedBytes = 0;
    double compressionRatio = 0.0;
    bool recommendStreaming = false;
    std::vector<AudioChannelInspection> channels;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Codec-neutral signal and import-budget diagnostics for decoded PCM. */
class AudioImportDiagnosticsService {
public:
    AudioImportInspectionResult inspect(const AudioImportInspectionRequest& request,
                                        const EditorAudioPcm& pcm) const;
};

}  // namespace eve::editor
