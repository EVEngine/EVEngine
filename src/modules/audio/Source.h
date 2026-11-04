#pragma once
#include "common/Export.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "AudioTypes.h"
#include "common/Result.h"

namespace eve {
namespace sound {
class SoundData;
class Decoder;
}  // namespace sound
namespace audio {

class Audio;

/**
 * @brief A playable audio source (static buffer or streaming decoder).
 * Not thread-safe for playback control except fillPendingFromDecoder(), which is
 * synchronized for the Audio worker thread.
 */
class EVENGINE_API_BACKENDS Source {
public:
    /** @brief Creates a static source from decoded audio (both arguments required). */
    Source(Audio *audio, sound::SoundData *data);
    /** @brief Creates a streaming (or static-decoder) source; ownership of decoder is optional. */
    Source(Audio *audio, sound::Decoder *decoder, bool streaming, bool takeDecoderOwnership = false);
    /** @brief Source. */
    ~Source();

    /** @brief Starts (or resumes) playback. */
    void play();
    /** @brief Pauses playback, keeping the play position. */
    void pause();
    /** @brief Stops playback and rewinds the play position. */
    void stop();
    /** @brief True when playing. */
    bool isPlaying() const;

    /** @brief Sets source gain (clamped to >= 0). Does not cancel an active fade target. */
    void setVolume(float v);
    /** @brief Returns the audible user volume after the last advanceFade (or committed volume). */
    float getVolume() const;
    /** @brief Sets playback pitch (clamped to >= 0). */
    void setPitch(float p);
    /** @brief Returns the pitch. */
    float getPitch() const;
    /** @brief Enables/disables looping. */
    void setLooping(bool l);
    /** @brief True when looping. */
    bool isLooping() const;

    /** @brief Seeks to a time in seconds; false when seeking is unsupported. */
    bool seek(double seconds);
    /** @brief Current play time in seconds. */
    double tell() const;
    /** @brief Total duration in seconds (0 for live/unbounded streams). */
    double getDuration() const;

    /** @brief Sets the source position in world units. */
    void setPosition(float x, float y, float z);
    /** @brief Sets the source velocity (used by OpenAL doppler). */
    void setVelocity(float x, float y, float z);
    /** @brief Sets the source directional cone orientation. */
    void setDirection(float x, float y, float z);
    /** @brief Makes the source ignore the listener position (head-relative). */
    void setRelative(bool relative);
    /** @brief True when the source is head-relative. */
    [[nodiscard]] bool isRelative() const noexcept { return relative_; }
    /** @brief Sets OpenAL reference and maximum attenuation distances. */
    void setAttenuationDistances(float ref, float max);

    /**
     * @brief Ramp audible volume toward targetVolume using injected simulation time.
     * @param targetVolume Destination user volume (>= 0).
     * @param durationSeconds Non-negative ramp length; 0 snaps immediately.
     * @param now Absolute simulation time in seconds.
     */
    [[nodiscard]] Result<void> fadeTo(float targetVolume, float durationSeconds, float now);
    /**
     * @brief Fade audible volume to zero then stop when the ramp completes.
     * @param durationSeconds Non-negative ramp length; 0 stops immediately.
     * @param now Absolute simulation time in seconds.
     */
    [[nodiscard]] Result<void> fadeOutAndStop(float durationSeconds, float now);
    /** @brief Abort an active fade, keeping the current audible volume. */
    [[nodiscard]] Result<void> cancelFade();
    /**
     * @brief Advance an active fade envelope to absolute simulation time `now`.
     * @details Idempotent. On completion commits volume to the fade target and
     * stops when fadeOutAndStop requested silence.
     */
    [[nodiscard]] Result<void> advanceFade(float now);
    /** @brief Fade progress in [0,1]; 1 when idle. */
    [[nodiscard]] float getFadeWeight() const noexcept { return fadeWeight_; }
    /** @brief True while a fade envelope is active. */
    [[nodiscard]] bool isFading() const noexcept { return fading_; }

    /**
     * @brief Continuous 2D/3D mix: 0 = ambient bed (relative, no rolloff), 1 = full world.
     * @details Stereo with blend > 0 requires AL_SOFT_source_spatialize.
     */
    [[nodiscard]] Result<void> setSpatialBlend(float blend);
    /** @brief Returns the spatial blend in [0,1]. */
    [[nodiscard]] float getSpatialBlend() const noexcept { return spatialBlend_; }

    /**
     * @brief Stereo/ambient balance in [-1,1] (left..right).
     * @details Prefers AL_EXT_STEREO_ANGLES. Without it, relative mono uses position.x;
     * relative stereo without the extension fails.
     */
    [[nodiscard]] Result<void> setBalance(float balance);
    /** @brief Returns the balance in [-1,1]. */
    [[nodiscard]] float getBalance() const noexcept { return balance_; }

    /** @brief Sets OpenAL rolloff factor (clamped to >= 0). */
    [[nodiscard]] Result<void> setRolloffFactor(float factor);
    /** @brief Returns the rolloff factor. */
    [[nodiscard]] float getRolloffFactor() const noexcept { return rolloffFactor_; }

    /** @brief Channel count of the bound buffer/decoder (1 or 2), or 0 when unknown. */
    [[nodiscard]] int getChannelCount() const;

    /** @brief True when streaming. */
    bool isStreaming() const { return streaming; }
    /** @brief Main thread: queues/unqueues AL buffers for a streaming source. */
    void pump();

    /**
     * @brief Called by the Audio worker thread. Internally synchronized against
     * concurrent decoder teardown (see decoderMutex) — safe to call even if
     * another thread is stopping/seeking/destroying this Source.
     */
    void fillPendingFromDecoder();

private:
    friend class Audio;

    void                       applyGainPitch();
    void                       applyRolloff();
    void                       applyBalanceToAl();
    ALenum                     alFormat() const;
    void                       ensureStaticBuffer();
    int                        decoderSampleRateOr(int fallback) const;
    float                      currentAudibleVolume() const noexcept;
    [[nodiscard]] Result<void> beginFade(float targetVolume, float durationSeconds, float now, bool stopWhenSilent);

    Audio*            audio       = nullptr;
    sound::SoundData* staticData  = nullptr;
    sound::Decoder*   decoder     = nullptr;
    bool              ownsDecoder = false;
    bool              streaming   = false;

    ALuint alSource = 0;
    ALuint alBuffer = 0;
    ALuint streamBuffers[kStreamBufferCount]{};
    int    streamBufferCount = 0;

    float volume                   = 1.f;
    float audibleVolume_           = 1.f;
    float pitch                    = 1.f;
    bool  looping                  = false;
    bool  relative_                = false;
    float spatialBlend_            = 1.f;
    float balance_                 = 0.f;
    float rolloffFactor_           = 1.f;
    float positionX_               = 0.f;
    float positionY_               = 0.f;
    float positionZ_               = 0.f;
    bool  balanceUsesStereoAngles_ = false;
    bool  balanceUsesMonoPosition_ = false;

    bool  fading_         = false;
    bool  stopWhenSilent_ = false;
    float fadeWeight_     = 1.f;
    float fadeFrom_       = 1.f;
    float fadeToVolume_   = 1.f;
    float fadeStart_      = 0.f;
    float fadeEnd_        = 0.f;

    // Guards all access to `decoder` (including its lifetime): the Audio worker
    // thread reads/decodes via fillPendingFromDecoder() while other threads may
    // seek/stop/destroy the Source (and delete its Decoder) concurrently.
    mutable std::mutex decoderMutex;

    std::mutex           pendingMutex;
    std::queue<PcmChunk> pending;
    std::atomic<bool>    playing{false};
    std::atomic<bool>    wantsData{false};
};

}  // namespace audio
}  // namespace eve
