#pragma once

namespace eve::audio {
class Audio;

/**
 * @brief Bind the live Audio module into the platform event pump sink.
 * @ownership Borrowed; cleared when the Audio module is destroyed.
 * @lifetime Valid from Audio construction until Audio destruction.
 */
void bindAudioEventSinkOwner(Audio* owner) noexcept;

}  // namespace eve::audio
