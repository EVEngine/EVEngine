#include "Source.h"
#include "Audio.h"

#include "common/Exception.h"

#include <algorithm>
#include <cstring>

namespace eve {
namespace audio {

namespace {
constexpr size_t kMaxPendingChunks = 8;
}

Source::Source(Audio *audio, sound::SoundData *data)
    : audio(audio), staticData(data), streaming(false) {
    if (!audio || !data)
        throw eve::Exception("Invalid Source arguments");
    alGenSources(1, &alSource);
    ensureStaticBuffer();
    applyGainPitch();
    alSourcei(alSource, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}

Source::Source(Audio *audio, sound::Decoder *decoder, bool streaming, bool takeDecoderOwnership)
    : audio(audio), decoder(decoder), ownsDecoder(takeDecoderOwnership), streaming(streaming) {
    if (!audio || !decoder)
        throw eve::Exception("Invalid Source arguments");
    alGenSources(1, &alSource);
    if (streaming) {
        streamBufferCount = kStreamBufferCount;
        alGenBuffers(streamBufferCount, streamBuffers);
        wantsData = true;
        audio->registerStream(this);
        audio->notifyWorker();
    } else {
        // Decode fully to static buffer via temporary SoundData path in Audio factory.
        throw eve::Exception("Non-stream Decoder Source should be created via SoundData");
    }
    applyGainPitch();
}

Source::~Source() {
    stop();
    if (streaming && audio)
        audio->unregisterStream(this);
    if (alSource) {
        alSourceStop(alSource);
        alSourcei(alSource, AL_BUFFER, 0);
        alDeleteSources(1, &alSource);
        alSource = 0;
    }
    if (alBuffer) {
        alDeleteBuffers(1, &alBuffer);
        alBuffer = 0;
    }
    if (streamBufferCount > 0) {
        alDeleteBuffers(streamBufferCount, streamBuffers);
        streamBufferCount = 0;
    }
    if (ownsDecoder) {
        delete decoder;
        decoder = nullptr;
    }
}

ALenum Source::alFormat() const {
    int ch = 0, bits = 0;
    if (staticData) {
        ch = staticData->getChannelCount();
        bits = staticData->getBitDepth();
    } else if (decoder) {
        ch = decoder->getChannelCount();
        bits = decoder->getBitDepth();
    }
    if (ch == 1 && bits == 8) return AL_FORMAT_MONO8;
    if (ch == 1 && bits == 16) return AL_FORMAT_MONO16;
    if (ch == 2 && bits == 8) return AL_FORMAT_STEREO8;
    if (ch == 2 && bits == 16) return AL_FORMAT_STEREO16;
    throw eve::Exception("Unsupported PCM format for OpenAL");
}

void Source::ensureStaticBuffer() {
    if (alBuffer || !staticData)
        return;
    alGenBuffers(1, &alBuffer);
    alBufferData(alBuffer, alFormat(), staticData->getData(),
                 static_cast<ALsizei>(staticData->getSize()), staticData->getSampleRate());
    alSourcei(alSource, AL_BUFFER, static_cast<ALint>(alBuffer));
}

void Source::applyGainPitch() {
    float master = audio ? audio->getVolume() : 1.f;
    alSourcef(alSource, AL_GAIN, volume * master);
    alSourcef(alSource, AL_PITCH, pitch);
}

void Source::play() {
    if (streaming) {
        playing = true;
        wantsData = true;
        if (audio)
            audio->notifyWorker();
        alSourcePlay(alSource);
        return;
    }
    ensureStaticBuffer();
    applyGainPitch();
    alSourcePlay(alSource);
    playing = true;
}

void Source::pause() {
    alSourcePause(alSource);
    playing = false;
}

void Source::stop() {
    alSourceStop(alSource);
    playing = false;
    if (streaming) {
        // Unqueue all
        ALint processed = 0;
        alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint b = 0;
            alSourceUnqueueBuffers(alSource, 1, &b);
        }
        ALint queued = 0;
        alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0) {
            ALuint b = 0;
            alSourceUnqueueBuffers(alSource, 1, &b);
        }
        std::lock_guard<std::mutex> lock(pendingMutex);
        while (!pending.empty())
            pending.pop();
        if (decoder)
            decoder->rewind();
        wantsData = true;
    } else {
        alSourceRewind(alSource);
    }
}

bool Source::isPlaying() const {
    ALint state = 0;
    alGetSourcei(alSource, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

void Source::setVolume(float v) {
    volume = std::max(0.f, v);
    applyGainPitch();
}
float Source::getVolume() const { return volume; }

void Source::setPitch(float p) {
    pitch = std::max(0.001f, p);
    applyGainPitch();
}
float Source::getPitch() const { return pitch; }

void Source::setLooping(bool l) {
    looping = l;
    if (!streaming)
        alSourcei(alSource, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}
bool Source::isLooping() const { return looping; }

bool Source::seek(double seconds) {
    if (streaming) {
        if (!decoder || !decoder->isSeekable())
            return false;
        stop();
        if (!decoder->seek(seconds))
            return false;
        wantsData = true;
        if (audio)
            audio->notifyWorker();
        return true;
    }
    alSourcef(alSource, AL_SEC_OFFSET, static_cast<ALfloat>(seconds));
    return true;
}

double Source::tell() const {
    ALfloat sec = 0.f;
    alGetSourcef(alSource, AL_SEC_OFFSET, &sec);
    return static_cast<double>(sec);
}

double Source::getDuration() const {
    if (staticData)
        return staticData->getDuration();
    if (decoder)
        return decoder->getDuration();
    return -1.0;
}

void Source::setPosition(float x, float y, float z) { alSource3f(alSource, AL_POSITION, x, y, z); }
void Source::setVelocity(float x, float y, float z) { alSource3f(alSource, AL_VELOCITY, x, y, z); }
void Source::setDirection(float x, float y, float z) { alSource3f(alSource, AL_DIRECTION, x, y, z); }
void Source::setRelative(bool relative) {
    alSourcei(alSource, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
}
void Source::setAttenuationDistances(float ref, float max) {
    alSourcef(alSource, AL_REFERENCE_DISTANCE, ref);
    alSourcef(alSource, AL_MAX_DISTANCE, max);
    alSourcef(alSource, AL_ROLLOFF_FACTOR, 1.f);
}

void Source::fillPendingFromDecoder() {
    if (!streaming || !decoder || !wantsData.load())
        return;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        if (pending.size() >= kMaxPendingChunks)
            return;
    }
    int n = decoder->decode();
    if (n <= 0) {
        if (looping && decoder->rewind()) {
            n = decoder->decode();
        } else {
            wantsData = false;
            return;
        }
    }
    if (n <= 0)
        return;
    PcmChunk chunk;
    auto *buf = static_cast<const uint8_t *>(decoder->getBuffer());
    chunk.bytes.assign(buf, buf + n);
    std::lock_guard<std::mutex> lock(pendingMutex);
    pending.push(std::move(chunk));
}

void Source::pump() {
    if (!streaming || !alSource)
        return;

    ALint processed = 0;
    alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint b = 0;
        alSourceUnqueueBuffers(alSource, 1, &b);
        PcmChunk chunk;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pending.empty()) {
                // Recycle empty: leave buffer unused until data arrives
                // Re-queue nothing; keep buffer id available by... we need free list.
                // Simpler: if no pending, push buffer id back by queuing silence? Skip.
                // Keep a free list via re-queue of zero later - for now re-store by
                // temporarily using pending empty and regenerating: actually alGen keeps
                // buffer; we must not lose buffer id. Queue a tiny silence.
                std::vector<uint8_t> silence(256, 0);
                alBufferData(b, alFormat(), silence.data(), static_cast<ALsizei>(silence.size()),
                             decoder ? decoder->getSampleRate() : 44100);
                alSourceQueueBuffers(alSource, 1, &b);
                continue;
            }
            chunk = std::move(pending.front());
            pending.pop();
        }
        alBufferData(b, alFormat(), chunk.bytes.data(), static_cast<ALsizei>(chunk.bytes.size()),
                     decoder->getSampleRate());
        alSourceQueueBuffers(alSource, 1, &b);
    }

    // Initial fill: queue unused buffers
    ALint queued = 0;
    alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
    for (int i = queued; i < streamBufferCount; ++i) {
        PcmChunk chunk;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pending.empty())
                break;
            chunk = std::move(pending.front());
            pending.pop();
        }
        ALuint b = streamBuffers[i];
        // Find a buffer not currently queued — for initial, buffers 0..n unused
        // Use streamBuffers[queued] style
        ALuint buf = streamBuffers[queued];
        alBufferData(buf, alFormat(), chunk.bytes.data(), static_cast<ALsizei>(chunk.bytes.size()),
                     decoder->getSampleRate());
        alSourceQueueBuffers(alSource, 1, &buf);
        alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
    }

    if (playing && !isPlaying()) {
        ALint q = 0;
        alGetSourcei(alSource, AL_BUFFERS_QUEUED, &q);
        if (q > 0)
            alSourcePlay(alSource);
    }

    wantsData = true;
    if (audio)
        audio->notifyWorker();
}

}  // namespace audio
}  // namespace eve
