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

class Source : public Object {
public:
    Source(Audio *audio, sound::SoundData *data);
    Source(Audio *audio, sound::Decoder *decoder, bool streaming, bool takeDecoderOwnership = false);
    ~Source() override;

    void play();
    void pause();
    void stop();
    bool isPlaying() const;

    void setVolume(float v);
    float getVolume() const;
    void setPitch(float p);
    float getPitch() const;
    void setLooping(bool l);
    bool isLooping() const;

    bool seek(double seconds);
    double tell() const;
    double getDuration() const;

    void setPosition(float x, float y, float z);
    void setVelocity(float x, float y, float z);
    void setDirection(float x, float y, float z);
    void setRelative(bool relative);
    void setAttenuationDistances(float ref, float max);

    bool isStreaming() const { return streaming; }
    void pump(); // main thread: queue/unqueue AL buffers for stream

    // Called by Audio worker under Audio mutex.
    void fillPendingFromDecoder();

private:
    friend class Audio;

    void applyGainPitch();
    ALenum alFormat() const;
    void ensureStaticBuffer();

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

    std::mutex pendingMutex;
    std::queue<PcmChunk> pending;
    std::atomic<bool> playing{false};
    std::atomic<bool> wantsData{false};
};

}  // namespace audio
}  // namespace eve
