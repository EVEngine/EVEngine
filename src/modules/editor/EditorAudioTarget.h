#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTarget.h"

/**
 * @file
 * @brief Compatibility include for audio authoring targets.
 *
 * The canonical implementation belongs to the optional audio_editing
 * satellite. New code should include audio_editing/AudioTarget.h directly.
 */
#include "audio_editing/AudioTarget.h"

namespace eve::editor {
using AudioSourceTarget           = audio_editing::AudioSourceTarget;
using IAudioSourceRuntimeSink     = audio_editing::IAudioSourceRuntimeSink;
using AudioSourcePublishingTarget = audio_editing::AudioSourcePublishingTarget;
using AudioBusSnapshot            = audio_editing::AudioBusSnapshot;
using AudioMixerTarget            = audio_editing::AudioMixerTarget;
using AudioSourceRuntimeApplier   = audio_editing::AudioSourceRuntimeApplier;
using AudioSourceRuntimeSink      = audio_editing::AudioSourceRuntimeSink;
}  // namespace eve::editor
