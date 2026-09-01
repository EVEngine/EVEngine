#pragma once

#include "audio_editor/EditorAudioTarget.h"

/**
 * @file
 * @brief Compatibility include for audio effect-chain authoring.
 *
 * The canonical implementation belongs to the optional audio_editing
 * satellite. New code should include audio_editing/AudioEffects.h directly.
 */
#include "audio_editing/AudioEffects.h"

namespace eve::editor {
using AudioEffectRecord         = audio_editing::AudioEffectRecord;
using AudioEffectChainTarget    = audio_editing::AudioEffectChainTarget;
using IAudioEffectChainSink     = audio_editing::IAudioEffectChainSink;
using AudioEffectChainPublisher = audio_editing::AudioEffectChainPublisher;
}  // namespace eve::editor
