#include "Source.h"
#include "Audio.h"

#include "common/Exception.h"
#include "sound/Decoder.h"
#include "sound/SoundData.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef AL_STEREO_ANGLES
#define AL_STEREO_ANGLES 0x1030
#endif
#ifndef AL_SOURCE_SPATIALIZE_SOFT
#define AL_SOURCE_SPATIALIZE_SOFT 0x1214
#endif

namespace eve {
namespace audio {

namespace {
constexpr size_t kMaxPendingChunks = 8;

bool hasAlExtension(const char* name) { return name && alIsExtensionPresent(name) == AL_TRUE; }

Diagnostic invalidAudioArg(const char* message, const char* path = "audio.source") {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, path);
}
}  // namespace

Source::Source(Audio* audio, sound::SoundData* data) : audio(audio), staticData(data), streaming(false) {
    if (!audio || !data) throw eve::Exception("Invalid Source arguments");
    alGenSources(1, &alSource);
    ensureStaticBuffer();
    applyGainPitch();
    alSourcei(alSource, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}

Source::Source(Audio *audio, sound::Decoder *decoder, bool streaming, bool takeDecoderOwnership)
    : audio(audio), decoder(decoder), ownsDecoder(takeDecoderOwnership), streaming(streaming) {
    if (!audio || !decoder) throw eve::Exception("Invalid Source arguments");
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
    if (audio) {
        // Static sources are tracked in allSources but never registered as streams;
        // always detach so stopAll cannot touch a deleted Source.
        audio->unregisterSource(this);
        audio = nullptr;
    }
    if (alSource) {
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
    {
        // Audio::workerMain()/pump()/stopAll() hold Audio::mutex across the whole
        // dispatch loop, and unregisterSource() above took that same mutex, so no
        // in-flight fillPendingFromDecoder() can still reference this Source. The
        // decoderMutex lock below remains as defense in depth before freeing the
        // decoder (and the mutexes themselves are destroyed only after this body).
        std::lock_guard<std::mutex> lock(decoderMutex);
        if (ownsDecoder) {
            delete decoder;
            decoder = nullptr;
        }
    }
}

ALenum Source::alFormat() const {
    int ch = 0, bits = 0;
    if (staticData) {
        ch   = staticData->getChannelCount();
        bits = staticData->getBitDepth();
    } else {
        std::lock_guard<std::mutex> lock(decoderMutex);
        if (decoder) {
            ch   = decoder->getChannelCount();
            bits = decoder->getBitDepth();
        }
    }
    if (ch == 1 && bits == 8) return AL_FORMAT_MONO8;
    if (ch == 1 && bits == 16) return AL_FORMAT_MONO16;
    if (ch == 2 && bits == 8) return AL_FORMAT_STEREO8;
    if (ch == 2 && bits == 16) return AL_FORMAT_STEREO16;
    throw eve::Exception("Unsupported PCM format for OpenAL");
}

int Source::decoderSampleRateOr(int fallback) const {
    std::lock_guard<std::mutex> lock(decoderMutex);
    return decoder ? decoder->getSampleRate() : fallback;
}

int Source::getChannelCount() const {
    if (staticData) return staticData->getChannelCount();
    std::lock_guard<std::mutex> lock(decoderMutex);
    return decoder ? decoder->getChannelCount() : 0;
}

void Source::ensureStaticBuffer() {
    if (alBuffer || !staticData) return;
    alGenBuffers(1, &alBuffer);
    alBufferData(alBuffer, alFormat(), staticData->getData(), static_cast<ALsizei>(staticData->getSize()),
                 staticData->getSampleRate());
    alSourcei(alSource, AL_BUFFER, static_cast<ALint>(alBuffer));
}

float Source::currentAudibleVolume() const noexcept { return fading_ ? audibleVolume_ : volume; }

void Source::applyGainPitch() {
    float       master  = audio ? audio->getVolume() : 1.f;
    const float audible = fading_ ? audibleVolume_ : volume;
    alSourcef(alSource, AL_GAIN, audible * master * (fading_ ? 1.f : fadeWeight_));
    alSourcef(alSource, AL_PITCH, pitch);
}

void Source::applyRolloff() {
    if (alSource) alSourcef(alSource, AL_ROLLOFF_FACTOR, rolloffFactor_);
}

void Source::applyBalanceToAl() {
    if (!alSource) return;
    if (balanceUsesStereoAngles_) {
        // OpenAL Soft: angles are radians from forward; +/- (pi/2)*(1-|balance|) style pan.
        const float halfPi    = 1.57079632679f;
        const float left      = -halfPi + balance_ * halfPi;
        const float right     = halfPi + balance_ * halfPi;
        const float angles[2] = {left, right};
        alSourcefv(alSource, AL_STEREO_ANGLES, angles);
        return;
    }
    if (balanceUsesMonoPosition_) {
        alSource3f(alSource, AL_POSITION, balance_, positionY_, positionZ_);
        return;
    }
    alSource3f(alSource, AL_POSITION, positionX_, positionY_, positionZ_);
}

void Source::play() {
    if (streaming) {
        playing   = true;
        wantsData = true;
        if (audio) audio->notifyWorker();
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
    if (!alSource) return;
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
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            while (!pending.empty()) pending.pop();
        }
        {
            std::lock_guard<std::mutex> lock(decoderMutex);
            if (decoder) decoder->rewind();
        }
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
    if (!fading_) {
        audibleVolume_ = volume;
        applyGainPitch();
    }
}
float Source::getVolume() const { return currentAudibleVolume(); }

void Source::setPitch(float p) {
    pitch = std::max(0.001f, p);
    applyGainPitch();
}
float Source::getPitch() const { return pitch; }

void Source::setLooping(bool l) {
    looping = l;
    if (!streaming) alSourcei(alSource, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}
bool Source::isLooping() const { return looping; }

bool Source::seek(double seconds) {
    if (streaming) {
        {
            std::lock_guard<std::mutex> lock(decoderMutex);
            if (!decoder || !decoder->isSeekable()) return false;
        }
        // stop() takes decoderMutex itself (for rewind), so it must not be held here.
        stop();
        {
            std::lock_guard<std::mutex> lock(decoderMutex);
            if (!decoder || !decoder->seek(seconds)) return false;
        }
        wantsData = true;
        if (audio) audio->notifyWorker();
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
    if (staticData) return staticData->getDuration();
    std::lock_guard<std::mutex> lock(decoderMutex);
    if (decoder) return decoder->getDuration();
    return -1.0;
}

void Source::setPosition(float x, float y, float z) {
    positionX_ = x;
    positionY_ = y;
    positionZ_ = z;
    if (balanceUsesMonoPosition_) {
        alSource3f(alSource, AL_POSITION, balance_, positionY_, positionZ_);
        return;
    }
    alSource3f(alSource, AL_POSITION, x, y, z);
}
void Source::setVelocity(float x, float y, float z) { alSource3f(alSource, AL_VELOCITY, x, y, z); }
void Source::setDirection(float x, float y, float z) { alSource3f(alSource, AL_DIRECTION, x, y, z); }
void Source::setRelative(bool relative) {
    relative_ = relative;
    alSourcei(alSource, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
}
void Source::setAttenuationDistances(float ref, float max) {
    alSourcef(alSource, AL_REFERENCE_DISTANCE, ref);
    alSourcef(alSource, AL_MAX_DISTANCE, max);
    applyRolloff();
}

Result<void> Source::setRolloffFactor(float factor) {
    if (!std::isfinite(factor) || factor < 0.f)
        return Result<void>::failure(invalidAudioArg("rolloff factor must be finite and >= 0", "rolloff"));
    rolloffFactor_ = factor;
    applyRolloff();
    return Result<void>::success();
}

Result<void> Source::setSpatialBlend(float blend) {
    if (!std::isfinite(blend) || blend < 0.f || blend > 1.f)
        return Result<void>::failure(invalidAudioArg("spatial blend must be finite and in [0,1]", "spatialBlend"));
    const int  channels       = getChannelCount();
    const bool softSpatialize = hasAlExtension("AL_SOFT_source_spatialize");
    if (channels == 2 && blend > 0.f && !softSpatialize)
        return Result<void>::failure(
            invalidAudioArg("stereo spatialBlend>0 requires AL_SOFT_source_spatialize; use mono or configureAmbientBed",
                            "spatialBlend"));
    if (softSpatialize && alSource) alSourcei(alSource, AL_SOURCE_SPATIALIZE_SOFT, blend > 0.f ? AL_TRUE : AL_FALSE);

    spatialBlend_ = blend;
    if (blend <= 0.f) {
        setRelative(true);
        rolloffFactor_ = 0.f;
    } else {
        setRelative(false);
        rolloffFactor_ = blend;
    }
    applyRolloff();
    return Result<void>::success();
}

Result<void> Source::setBalance(float balance) {
    if (!std::isfinite(balance) || balance < -1.f || balance > 1.f)
        return Result<void>::failure(invalidAudioArg("balance must be finite and in [-1,1]", "balance"));
    const int  channels      = getChannelCount();
    const bool stereoAngles  = hasAlExtension("AL_EXT_STEREO_ANGLES");
    balanceUsesStereoAngles_ = false;
    balanceUsesMonoPosition_ = false;
    if (stereoAngles && channels == 2) {
        balance_                 = balance;
        balanceUsesStereoAngles_ = true;
        applyBalanceToAl();
        return Result<void>::success();
    }
    if (relative_ && channels == 1) {
        balance_                 = balance;
        balanceUsesMonoPosition_ = true;
        applyBalanceToAl();
        return Result<void>::success();
    }
    if (channels == 2)
        return Result<void>::failure(invalidAudioArg("stereo balance requires AL_EXT_STEREO_ANGLES", "balance"));
    if (!relative_)
        return Result<void>::failure(
            invalidAudioArg("mono balance without stereo angles requires a relative source", "balance"));
    return Result<void>::failure(invalidAudioArg("balance is unsupported for this source format", "balance"));
}

Result<void> Source::beginFade(float targetVolume, float durationSeconds, float now, bool stopWhenSilent) {
    if (!std::isfinite(targetVolume) || targetVolume < 0.f || !std::isfinite(durationSeconds) ||
        durationSeconds < 0.f || !std::isfinite(now))
        return Result<void>::failure(
            invalidAudioArg("fade requires finite now, non-negative duration and volume", "fade"));
    if (durationSeconds == 0.f) {
        fading_         = false;
        stopWhenSilent_ = false;
        fadeWeight_     = 1.f;
        volume          = targetVolume;
        audibleVolume_  = targetVolume;
        applyGainPitch();
        if (stopWhenSilent && targetVolume == 0.f) stop();
        return Result<void>::success();
    }
    fadeFrom_       = currentAudibleVolume();
    fadeToVolume_   = targetVolume;
    fadeStart_      = now;
    fadeEnd_        = now + durationSeconds;
    fadeWeight_     = 0.f;
    fading_         = true;
    stopWhenSilent_ = stopWhenSilent;
    audibleVolume_  = fadeFrom_;
    applyGainPitch();
    return Result<void>::success();
}

Result<void> Source::fadeTo(float targetVolume, float durationSeconds, float now) {
    return beginFade(targetVolume, durationSeconds, now, false);
}

Result<void> Source::fadeOutAndStop(float durationSeconds, float now) {
    return beginFade(0.f, durationSeconds, now, true);
}

Result<void> Source::cancelFade() {
    if (!fading_) return Result<void>::success();
    volume          = audibleVolume_;
    fading_         = false;
    stopWhenSilent_ = false;
    fadeWeight_     = 1.f;
    applyGainPitch();
    return Result<void>::success();
}

Result<void> Source::advanceFade(float now) {
    if (!std::isfinite(now)) return Result<void>::failure(invalidAudioArg("advanceFade requires finite now", "fade"));
    if (!fading_) return Result<void>::success();
    if (now <= fadeStart_) {
        fadeWeight_    = 0.f;
        audibleVolume_ = fadeFrom_;
        applyGainPitch();
        return Result<void>::success();
    }
    if (now >= fadeEnd_ || fadeEnd_ <= fadeStart_) {
        volume                = fadeToVolume_;
        audibleVolume_        = fadeToVolume_;
        fadeWeight_           = 1.f;
        fading_               = false;
        const bool shouldStop = stopWhenSilent_ && fadeToVolume_ == 0.f;
        stopWhenSilent_       = false;
        applyGainPitch();
        if (shouldStop) stop();
        return Result<void>::success();
    }
    const float progress = (now - fadeStart_) / (fadeEnd_ - fadeStart_);
    fadeWeight_          = progress;
    audibleVolume_       = fadeFrom_ + (fadeToVolume_ - fadeFrom_) * progress;
    applyGainPitch();
    return Result<void>::success();
}

void Source::fillPendingFromDecoder() {
    if (!streaming || !wantsData.load()) return;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        if (pending.size() >= kMaxPendingChunks) return;
    }

    // Guard the whole decode step: `decoder` may be deleted concurrently by
    // Source::~Source() running on another thread (e.g. main/script thread
    // destroying this Source while the Audio worker is mid-decode here).
    PcmChunk chunk;
    {
        std::lock_guard<std::mutex> decoderLock(decoderMutex);
        if (!decoder) return;
        int n = decoder->decode();
        if (n <= 0) {
            if (looping && decoder->rewind()) {
                n = decoder->decode();
            } else {
                wantsData = false;
                return;
            }
        }
        if (n <= 0) return;
        auto *buf = static_cast<const uint8_t *>(decoder->getBuffer());
        chunk.bytes.assign(buf, buf + n);
    }

    std::lock_guard<std::mutex> lock(pendingMutex);
    pending.push(std::move(chunk));
}

void Source::pump() {
    if (!streaming || !alSource) return;

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
                             decoderSampleRateOr(44100));
                alSourceQueueBuffers(alSource, 1, &b);
                continue;
            }
            chunk = std::move(pending.front());
            pending.pop();
        }
        alBufferData(b, alFormat(), chunk.bytes.data(), static_cast<ALsizei>(chunk.bytes.size()),
                     decoderSampleRateOr(44100));
        alSourceQueueBuffers(alSource, 1, &b);
    }

    // Initial fill: queue unused buffers
    ALint queued = 0;
    alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
    for (int i = queued; i < streamBufferCount; ++i) {
        PcmChunk chunk;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pending.empty()) break;
            chunk = std::move(pending.front());
            pending.pop();
        }
        // Find a buffer not currently queued — for initial, buffers 0..n unused
        // Use streamBuffers[queued] style
        ALuint buf = streamBuffers[queued];
        alBufferData(buf, alFormat(), chunk.bytes.data(), static_cast<ALsizei>(chunk.bytes.size()),
                     decoderSampleRateOr(44100));
        alSourceQueueBuffers(alSource, 1, &buf);
        alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
    }

    if (playing && !isPlaying()) {
        ALint q = 0;
        alGetSourcei(alSource, AL_BUFFERS_QUEUED, &q);
        if (q > 0) alSourcePlay(alSource);
    }

    wantsData = true;
    if (audio) audio->notifyWorker();
}

}  // namespace audio
}  // namespace eve
