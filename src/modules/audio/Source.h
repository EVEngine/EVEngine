#pragma once

#include "common/Object.h"
#include "sound/SoundData.h"
#include "sound/Decoder.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "AudioTypes.h"

namespace eve {
namespace audio {

class Audio;

/**
 * @brief A playable audio source (static buffer or streaming decoder).
 * Not thread-safe for playback control except fillPendingFromDecoder(), which is
 * synchronized for the Audio worker thread.
 */
class Source : public Object {
public:
    /** @brief Creates a static source from decoded audio (both arguments required). */
    Source(Audio *audio, sound::SoundData *data);
    /** @brief Creates a streaming (or static-decoder) source; ownership of decoder is optional. */
    Source(Audio *audio, sound::Decoder *decoder, bool streaming, bool takeDecoderOwnership = false);
    ~Source() override;

    /** @brief Starts (or resumes) playback. */
    void play();
    /** @brief Pauses playback, keeping the play position. */
    void pause();
    /** @brief Stops playback and rewinds the play position. */
    void stop();
    bool isPlaying() const;

    /** @brief Sets source gain (clamped to >= 0). */
    void setVolume(float v);
    float getVolume() const;
    /** @brief Sets playback pitch (clamped to >= 0). */
    void setPitch(float p);
    float getPitch() const;
    /** @brief Enables/disables looping. */
    void setLooping(bool l);
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
    /** @brief Sets OpenAL reference and maximum attenuation distances. */
    void setAttenuationDistances(float ref, float max);

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

    void applyGainPitch();
    ALenum alFormat() const;
    void ensureStaticBuffer();
    int decoderSampleRateOr(int fallback) const;

    Audio *audio = nullptr;
    sound::SoundData *staticData = nullptr;
    sound::Decoder *decoder = nullptr;
    bool ownsDecoder = false;
    bool streaming = false;

    ALuint alSource = 0;
    ALuint alBuffer = 0;
    ALuint streamBuffers[kStreamBufferCount]{};
    int streamBufferCount = 0;

    float volume = 1.f;
    float pitch = 1.f;
    bool looping = false;

    // Guards all access to `decoder` (including its lifetime): the Audio worker
    // thread reads/decodes via fillPendingFromDecoder() while other threads may
    // seek/stop/destroy the Source (and delete its Decoder) concurrently.
    mutable std::mutex decoderMutex;

    std::mutex pendingMutex;
    std::queue<PcmChunk> pending;
    std::atomic<bool> playing{false};
    std::atomic<bool> wantsData{false};
};

}  // namespace audio
}  // namespace eve
