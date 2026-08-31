#pragma once

#include "audio_editing/AudioEditingTypes.h"

#include <string>
#include <vector>

namespace eve::audio_editing {

/** @brief Decoded interleaved PCM supplied by an audio importer or codec plugin. */
struct EditorAudioPcm {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;
};

/** @brief Immutable waveform request tied to an audio document revision. */
struct AudioWaveformRequest {
    std::string asset;
    Revision sourceRevision = 0;
    int pixelWidth = 0;
    double loopStart = 0.0;
    double loopEnd = 0.0;
};

/** @brief One min/max/RMS envelope bucket for a single channel. */
struct AudioWaveformBucket {
    double startTime = 0.0;
    double endTime = 0.0;
    float minimum = 0.0F;
    float maximum = 0.0F;
    float rms = 0.0F;
};

/** @brief Revision-tagged waveform and loop-preview diagnostics. */
struct AudioWaveformResult {
    EditorStatus status = EditorStatus::Failed;
    std::string asset;
    Revision sourceRevision = 0;
    int sampleRate = 0;
    int channels = 0;
    double duration = 0.0;
    int loopStartBucket = -1;
    int loopEndBucket = -1;
    double loopSeamDelta = 0.0;
    std::vector<std::vector<AudioWaveformBucket>> envelopes;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Codec-neutral waveform generation and loop QA service. */
class AudioWaveformService {
public:
    /** @brief Build bounded per-channel envelopes from already-decoded PCM. */
    AudioWaveformResult generate(const AudioWaveformRequest& request, const EditorAudioPcm& pcm,
                                 int maxPixels = 16384, std::size_t maxSamples = 100000000) const;
};

}  // namespace eve::audio_editing
