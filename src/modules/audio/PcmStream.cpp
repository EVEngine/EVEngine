// eve-hot-path
#include "PcmStream.h"

#include "Audio.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"

#include <AL/al.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace eve::audio {
struct PcmStreamState {
    ALuint                                    source = 0;
    std::array<ALuint, PcmStream::blockCount> buffers{};
    std::array<int, PcmStream::blockCount>    frames{};
    int                                       rate      = 0;
    int                                       channels  = 0;
    bool                                      wantsPlay = false;
    bool                                      ended     = false;
    bool                                      paused    = false;
    std::uint64_t                             played    = 0;
};

namespace {
Diagnostic closedDiagnostic() {
    return Diagnostic::error(DiagnosticCode::PreconditionViolation, "PCM stream is closed");
}

Result<void> alResult() {
    const auto error = alGetError();
    if (error != AL_NO_ERROR)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "OpenAL PCM operation failed",
                                                       "audio.pcm", {{"alError", std::to_string(error)}}));
    return Result<void>::success();
}
}  // namespace

PcmStream::PcmStream(std::shared_ptr<PcmStreamState> state) : state_(std::move(state)) {}
PcmStream::~PcmStream() { close(); }

Result<std::unique_ptr<PcmStream>> PcmStream::create(int sampleRate, int channels) {
    if (sampleRate < 8000 || sampleRate > 192000 || (channels != 1 && channels != 2))
        return Result<std::unique_ptr<PcmStream>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PCM requires 8000..192000 Hz and one or two channels"));
    auto state      = std::make_shared<PcmStreamState>();
    state->rate     = sampleRate;
    state->channels = channels;
    alGetError();
    alGenSources(1, &state->source);
    alGenBuffers(blockCount, state->buffers.data());
    alSourcei(state->source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcef(state->source, AL_ROLLOFF_FACTOR, 0.f);
    auto result = alResult();
    if (!result.ok()) {
        closeState(state);
        return Result<std::unique_ptr<PcmStream>>::failure(result.status());
    }
    return Result<std::unique_ptr<PcmStream>>::success(std::unique_ptr<PcmStream>(new PcmStream(std::move(state))));
}

void PcmStream::closeState(const std::shared_ptr<PcmStreamState>& state) {
    if (state->source) {
        alSourceStop(state->source);
        alSourcei(state->source, AL_BUFFER, 0);
        alDeleteSources(1, &state->source);
        state->source = 0;
    }
    for (auto& buffer : state->buffers) {
        if (buffer) alDeleteBuffers(1, &buffer);
        buffer = 0;
    }
    state->frames.fill(0);
}

void PcmStream::close() { closeState(state_); }

void PcmStream::pumpState(const std::shared_ptr<PcmStreamState>& state) {
    if (!state->source) return;
    ALint processed = 0;
    alGetSourcei(state->source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint buffer = 0;
        alSourceUnqueueBuffers(state->source, 1, &buffer);
        const auto found = std::find(state->buffers.begin(), state->buffers.end(), buffer);
        if (found != state->buffers.end()) {
            const auto index = static_cast<size_t>(found - state->buffers.begin());
            state->played += state->frames[index];
            state->frames[index] = 0;
        }
    }
    if (state->wantsPlay && !state->paused) {
        ALint playback = 0, queued = 0;
        alGetSourcei(state->source, AL_SOURCE_STATE, &playback);
        alGetSourcei(state->source, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0 && playback != AL_PLAYING) alSourcePlay(state->source);
    }
}

Result<int> PcmStream::submit(std::span<const float> samples) {
    auto& state = *state_;
    if (!state.source) return Result<int>::failure(closedDiagnostic());
    if (state.ended)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "PCM input has ended"));
    if (samples.empty() || samples.size() % state.channels != 0 ||
        samples.size() > static_cast<size_t>(maxBlockFrames * state.channels) ||
        std::any_of(samples.begin(), samples.end(), [](float v) { return !std::isfinite(v) || v < -1.f || v > 1.f; }))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "PCM samples must be finite, normalized, channel-aligned and bounded to 4096 frames"));
    pumpState(state_);
    const auto free = std::find(state.frames.begin(), state.frames.end(), 0);
    if (free == state.frames.end())
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "PCM queue is full; retry this block later"));
    const auto           index = static_cast<size_t>(free - state.frames.begin());
    std::vector<ALshort> pcm(samples.size());
    std::transform(samples.begin(), samples.end(), pcm.begin(),
                   [](float v) { return static_cast<ALshort>(std::lround(v < 0.f ? v * 32768.f : v * 32767.f)); });
    alGetError();
    alBufferData(state.buffers[index], state.channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, pcm.data(),
                 static_cast<ALsizei>(pcm.size() * sizeof(ALshort)), state.rate);
    auto upload = alResult();
    if (!upload.ok()) return Result<int>::failure(upload.status());
    alSourceQueueBuffers(state.source, 1, &state.buffers[index]);
    auto queued = alResult();
    if (!queued.ok()) return Result<int>::failure(queued.status());
    const int accepted = static_cast<int>(samples.size() / state.channels);
    *free              = accepted;
    pumpState(state_);
    return Result<int>::success(accepted);
}

Result<void> PcmStream::play() {
    if (!state_->source) return Result<void>::failure(closedDiagnostic());
    state_->wantsPlay = true;
    state_->paused    = false;
    alGetError();
    pumpState(state_);
    return alResult();
}

Result<void> PcmStream::pause() {
    if (!state_->source) return Result<void>::failure(closedDiagnostic());
    alGetError();
    alSourcePause(state_->source);
    auto result = alResult();
    if (!result.ok()) return result;
    state_->paused = true;
    return Result<void>::success();
}

Result<void> PcmStream::finishInput() {
    if (!state_->source) return Result<void>::failure(closedDiagnostic());
    state_->ended = true;
    return Result<void>::success();
}

Result<void> PcmStream::reset() {
    if (!state_->source) return Result<void>::failure(closedDiagnostic());
    alGetError();
    alSourceStop(state_->source);
    alSourcei(state_->source, AL_BUFFER, 0);
    auto result = alResult();
    if (!result.ok()) return result;
    state_->frames.fill(0);
    state_->played    = 0;
    state_->wantsPlay = state_->ended = state_->paused = false;
    return Result<void>::success();
}

PcmStreamInfo PcmStream::getInfo() const {
    PcmStreamInfo info;
    info.sampleRate   = state_->rate;
    info.channels     = state_->channels;
    info.playedFrames = state_->played;
    for (int frames : state_->frames) {
        info.queuedFrames += frames;
        if (!frames) ++info.availableBlocks;
    }
    if (!state_->source) {
        info.phase           = PcmStreamPhase::Closed;
        info.availableBlocks = 0;
    } else if (state_->ended && !info.queuedFrames)
        info.phase = PcmStreamPhase::Finished;
    else if (state_->paused)
        info.phase = PcmStreamPhase::Paused;
    else if (!state_->wantsPlay)
        info.phase = PcmStreamPhase::Idle;
    else if (!info.queuedFrames)
        info.phase = PcmStreamPhase::Starved;
    else
        info.phase = state_->ended ? PcmStreamPhase::Draining : PcmStreamPhase::Playing;
    return info;
}

void exposePcmStream(ssq::Table& table, ssq::Class& audioClass) {
    auto cls = table.addClass<PcmStream>(
        "PcmStream",
        std::function<PcmStream*()>([]() -> PcmStream* { throw std::runtime_error("Use audio.newPcmStream"); }), false);
    const auto vm = table.getHandle();
    cls.addFunc("submit", [vm](PcmStream* self, ssq::Array samples) {
        if (samples.size() > PcmStream::maxBlockFrames * 2)
            return eve::script::projectResult(vm,
                                              Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                     "PCM block exceeds sample limit")),
                                              [](int n) { return Value(n); });
        std::vector<float> values;
        values.reserve(samples.size());
        try {
            for (size_t i = 0; i < samples.size(); ++i) values.push_back(samples.get<float>(i));
        } catch (const ssq::Exception&) {
            return eve::script::projectResult(
                vm,
                Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "PCM samples must be numbers")),
                [](int n) { return Value(n); });
        }
        return eve::script::projectResult(vm, self->submit(values), [](int n) { return Value(n); });
    });
    cls.addFunc("play", [vm](PcmStream* self) { return eve::script::projectResult(vm, self->play()); });
    cls.addFunc("pause", [vm](PcmStream* self) { return eve::script::projectResult(vm, self->pause()); });
    cls.addFunc("finishInput", [vm](PcmStream* self) { return eve::script::projectResult(vm, self->finishInput()); });
    cls.addFunc("reset", [vm](PcmStream* self) { return eve::script::projectResult(vm, self->reset()); });
    cls.addFunc("close", &PcmStream::close);
    cls.addFunc("getInfo", [vm](PcmStream* self) {
        const auto           info  = self->getInfo();
        constexpr std::array names = {"idle", "playing", "paused", "starved", "draining", "finished", "closed"};
        ssq::Table           snapshot(vm);
        snapshot.set("phase", std::string(names[static_cast<size_t>(info.phase)]));
        snapshot.set("sampleRate", info.sampleRate);
        snapshot.set("channels", info.channels);
        snapshot.set("availableBlocks", info.availableBlocks);
        snapshot.set("maxBlockFrames", PcmStream::maxBlockFrames);
        snapshot.set("queuedFrames", info.queuedFrames);
        snapshot.set("playedFrames", static_cast<std::int64_t>(info.playedFrames));
        return snapshot;
    });
    audioClass.addFunc("newPcmStream", [vm](Audio* self, int rate, int channels) {
        auto result = self->newPcmStream(rate, channels);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status());
        auto owned = eve::script::makeOwnedSquirrelInstance(vm, std::move(result).takeValue());
        if (!owned.ok()) return eve::script::projectStatusResult(vm, owned.status());
        return eve::script::projectStatusResult(vm, Status::success(), std::move(owned).takeValue());
    });
}
}  // namespace eve::audio
