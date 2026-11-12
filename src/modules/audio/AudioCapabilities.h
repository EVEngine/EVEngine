#pragma once
#include <memory>

namespace eve::audio {
class Audio;
/**
 * @brief Module-owned audio capability registrations and injected dependency lifetime.
 * @ownership Audio owns this object; retained action clients share an invalidatable binding.
 * @lifetime Destroy before Audio closes its device. Destruction revokes capabilities and
 * releases client sources while Audio is still alive; later device-dependent client calls report NotFound.
 * @thread Audio/main thread only. @reentrancy Destruction must not run inside an audio callback.
 */
class AudioCapabilities {
public:
    /** @brief Publish capabilities only after the audio device is initialized. */
    explicit AudioCapabilities(Audio& audio);
    /** @brief Revoke registrations and invalidate all retained clients. */
    ~AudioCapabilities();
    AudioCapabilities(const AudioCapabilities&)            = delete;
    AudioCapabilities& operator=(const AudioCapabilities&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
