#pragma once

#include "editor/EditorProtocol.h"

/**
 * @file
 * @brief Compatibility include for audio audition transport.
 *
 * The canonical implementation belongs to the optional audio_editing
 * satellite. New code should include audio_editing/AudioTransport.h directly.
 */
#include "audio_editing/AudioTransport.h"

namespace eve::editor {
using AudioTransportState         = audio_editing::AudioTransportState;
using AudioTransportSnapshot      = audio_editing::AudioTransportSnapshot;
using IAudioTransportBackend      = audio_editing::IAudioTransportBackend;
using AudioAuditionTransport      = audio_editing::AudioAuditionTransport;
using AudioSourceTransportBackend = audio_editing::AudioSourceTransportBackend;
}  // namespace eve::editor
