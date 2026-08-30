#pragma once

#include "editor/EditorProtocol.h"

/**
 * @file
 * @brief Compatibility include for audio waveform analysis.
 *
 * The canonical implementation belongs to the optional audio_editing
 * satellite. New code should include audio_editing/AudioWaveform.h directly.
 */
#include "audio_editing/AudioWaveform.h"

namespace eve::editor {
using EditorAudioPcm       = audio_editing::EditorAudioPcm;
using AudioWaveformRequest = audio_editing::AudioWaveformRequest;
using AudioWaveformBucket  = audio_editing::AudioWaveformBucket;
using AudioWaveformResult  = audio_editing::AudioWaveformResult;
using AudioWaveformService = audio_editing::AudioWaveformService;
}  // namespace eve::editor
