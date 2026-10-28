#include "audio/editing/AudioTransport.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "audio/editing/AudioWaveform.h"

// AudioTransport.cpp
namespace eve::audio_editing {
namespace {}  // namespace

Result<void> AudioAuditionTransport::bind(StableId asset, Revision revision, IAudioTransportBackend* backend) {
    if (asset.empty() || revision == 0 || !backend)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.audio.transport-bind"),
                                          "Audition requires asset identity, revision and playback backend");
    const double duration = backend->duration();
    if (!std::isfinite(duration) || duration <= 0.0)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.audio.transport-duration"),
                                          "Audition source requires a finite positive duration");
    unbind();
    asset_    = std::move(asset);
    revision_ = revision;
    backend_  = backend;
    backend_->setNativeLooping(false);
    return eve::editing::applied<void>();
}
Result<void> AudioAuditionTransport::validateRevision(Revision expected) const {
    if (!backend_)
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.audio.transport-unbound"),
                                          "No audition source is bound");
    if (expected != revision_)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.audio.transport-stale"),
                                          "Audition source revision is stale");
    return eve::editing::applied<void>();
}
Result<void> AudioAuditionTransport::setLoop(Revision expected, bool enabled, double start, double end) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    const double duration = backend_->duration();
    if (end == 0.0) end = duration;
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 || end <= start || end > duration)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.audio.transport-loop-range"),
                                          "Loop range must be finite, ordered and inside the clip");
    loopEnabled_ = enabled;
    loopStart_   = start;
    loopEnd_     = end;
    backend_->setNativeLooping(false);
    return eve::editing::applied<void>();
}
Result<void> AudioAuditionTransport::play(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    if (loopEnabled_ && (backend_->tell() < loopStart_ || backend_->tell() >= loopEnd_)) {
        auto seekResult = backend_->seek(loopStart_);
        if (!seekResult.ok()) return seekResult;
    }
    backend_->play();
    state_ = AudioTransportState::Playing;
    return eve::editing::applied<void>();
}
Result<void> AudioAuditionTransport::pause(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    if (state_ != AudioTransportState::Playing) {
        return eve::editing::noOp();
    }
    backend_->pause();
    state_ = AudioTransportState::Paused;
    return eve::editing::applied<void>();
}
Result<void> AudioAuditionTransport::stop(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    if (state_ == AudioTransportState::Stopped) {
        return eve::editing::noOp();
    }
    backend_->stop();
    state_ = AudioTransportState::Stopped;
    return eve::editing::applied<void>();
}
Result<void> AudioAuditionTransport::seek(Revision expected, double seconds) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    const double duration = backend_->duration();
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > duration)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.audio.transport-seek-range"),
                                          "Seek position must be inside the clip");
    return backend_->seek(seconds);
}
AudioTransportSnapshot AudioAuditionTransport::observe() const {
    return {asset_,
            revision_,
            state_,
            backend_ ? std::clamp(backend_->tell(), 0.0, backend_->duration()) : 0.0,
            backend_ ? backend_->duration() : 0.0,
            loopEnabled_,
            loopStart_,
            loopEnd_};
}
Result<AudioTransportSnapshot> AudioAuditionTransport::snapshot(Revision expected) const {
    auto valid = validateRevision(expected);
    if (!valid.ok())
        return eve::editing::failed<AudioTransportSnapshot>(valid.code(), RuleId("editor.audio.transport-stale"),
                                                            "Audition source is absent or stale");
    return eve::editing::applied<AudioTransportSnapshot>(observe());
}
Result<AudioTransportSnapshot> AudioAuditionTransport::update(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) {
        const EditorStatus status = valid.code();
        if (status == EditorStatus::Conflict) unbind();
        return eve::editing::failed<AudioTransportSnapshot>(status, RuleId("editor.audio.transport-stale"),
                                                            "Audition source is absent or stale");
    }
    if (state_ == AudioTransportState::Playing && loopEnabled_ && backend_->tell() >= loopEnd_) {
        auto seekResult = backend_->seek(loopStart_);
        if (!seekResult.ok())
            return eve::editing::failed<AudioTransportSnapshot>(
                seekResult.code(), RuleId("editor.audio.transport-loop-seek"), "Playback backend cannot wrap the loop");
        backend_->play();
    } else if (state_ == AudioTransportState::Playing && !backend_->playing() &&
               backend_->tell() >= backend_->duration()) {
        state_ = AudioTransportState::Stopped;
    }
    return eve::editing::applied<AudioTransportSnapshot>(observe());
}
void AudioAuditionTransport::unbind() {
    if (backend_) backend_->stop();
    backend_     = nullptr;
    asset_       = StableId{};
    revision_    = 0;
    state_       = AudioTransportState::Stopped;
    loopEnabled_ = false;
    loopStart_ = loopEnd_ = 0.0;
}

}  // namespace eve::audio_editing

// AudioWaveform.cpp
namespace eve::audio_editing {

AudioWaveformResult AudioWaveformService::generate(const AudioWaveformRequest& request, const EditorAudioPcm& pcm,
                                                   int maxPixels, std::size_t maxSamples) const {
    AudioWaveformResult result;
    result.asset          = request.asset;
    result.sourceRevision = request.sourceRevision;
    result.sampleRate     = pcm.sampleRate;
    result.channels       = pcm.channels;
    if (request.asset.empty() || request.pixelWidth <= 0 || request.pixelWidth > maxPixels || maxPixels <= 0 ||
        pcm.sampleRate <= 0 || pcm.channels <= 0 || pcm.channels > 32 || pcm.samples.empty() ||
        pcm.samples.size() > maxSamples || pcm.samples.size() % static_cast<std::size_t>(pcm.channels) != 0) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.audio.invalid-waveform-input"),
            DiagnosticSeverity::Error, "Waveform requires bounded, aligned PCM and a valid asset/sample format"));
        return result;
    }
    for (float sample : pcm.samples)
        if (!std::isfinite(sample)) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(
                eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId("editor.audio.nonfinite-pcm"),
                                             DiagnosticSeverity::Error, "Decoded PCM contains a non-finite sample"));
            return result;
        }
    const std::size_t frameCount = pcm.samples.size() / static_cast<std::size_t>(pcm.channels);
    result.duration              = static_cast<double>(frameCount) / pcm.sampleRate;
    const int buckets            = std::min<int>(request.pixelWidth, static_cast<int>(frameCount));
    result.envelopes.assign(static_cast<std::size_t>(pcm.channels), {});
    for (auto& channel : result.envelopes) channel.reserve(static_cast<std::size_t>(buckets));
    for (int bucket = 0; bucket < buckets; ++bucket) {
        const std::size_t begin = static_cast<std::size_t>(bucket) * frameCount / buckets;
        const std::size_t end   = std::max(begin + 1, static_cast<std::size_t>(bucket + 1) * frameCount / buckets);
        for (int channel = 0; channel < pcm.channels; ++channel) {
            float  minimum = std::numeric_limits<float>::infinity();
            float  maximum = -std::numeric_limits<float>::infinity();
            double squares = 0.0;
            for (std::size_t frame = begin; frame < end; ++frame) {
                const float sample = pcm.samples[frame * static_cast<std::size_t>(pcm.channels) + channel];
                minimum            = std::min(minimum, sample);
                maximum            = std::max(maximum, sample);
                squares += static_cast<double>(sample) * sample;
            }
            result.envelopes[static_cast<std::size_t>(channel)].push_back(
                {static_cast<double>(begin) / pcm.sampleRate, static_cast<double>(end) / pcm.sampleRate, minimum,
                 maximum, static_cast<float>(std::sqrt(squares / static_cast<double>(end - begin)))});
        }
    }
    if (request.loopEnd > 0.0 || request.loopStart > 0.0) {
        if (!std::isfinite(request.loopStart) || !std::isfinite(request.loopEnd) || request.loopStart < 0.0 ||
            request.loopEnd <= request.loopStart || request.loopEnd > result.duration) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("editor.audio.invalid-loop-range"),
                DiagnosticSeverity::Error, "Loop range must be ordered and within decoded clip duration"));
            return result;
        }
        const auto bucketAt = [&](double seconds) {
            return std::min(buckets - 1, static_cast<int>(seconds / result.duration * buckets));
        };
        result.loopStartBucket = bucketAt(request.loopStart);
        result.loopEndBucket   = bucketAt(request.loopEnd);
        const std::size_t startFrame =
            std::min(frameCount - 1, static_cast<std::size_t>(request.loopStart * pcm.sampleRate));
        const std::size_t endFrame =
            std::min(frameCount - 1, static_cast<std::size_t>(request.loopEnd * pcm.sampleRate));
        double maximumDelta = 0.0;
        for (int channel = 0; channel < pcm.channels; ++channel) {
            const float start = pcm.samples[startFrame * static_cast<std::size_t>(pcm.channels) + channel];
            const float end   = pcm.samples[endFrame * static_cast<std::size_t>(pcm.channels) + channel];
            maximumDelta      = std::max(maximumDelta, std::abs(static_cast<double>(end - start)));
        }
        result.loopSeamDelta = maximumDelta;
        if (maximumDelta > 0.1)
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("editor.audio.loop-seam-discontinuity"),
                DiagnosticSeverity::Warning, "Loop endpoints have a large amplitude discontinuity and may click"));
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::audio_editing
