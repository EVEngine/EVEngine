#include "audio_editing/AudioWaveform.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
            DiagnosticSeverity::Error,
            "Waveform requires bounded, aligned PCM and a valid asset/sample format"));
        return result;
    }
    for (float sample : pcm.samples)
        if (!std::isfinite(sample)) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvalidArgument, RuleId("editor.audio.nonfinite-pcm"),
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
                eve::DiagnosticCode::PreconditionViolation,
                RuleId("editor.audio.invalid-loop-range"), DiagnosticSeverity::Error,
                "Loop range must be ordered and within decoded clip duration"));
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
                eve::DiagnosticCode::PreconditionViolation,
                RuleId("editor.audio.loop-seam-discontinuity"), DiagnosticSeverity::Warning,
                "Loop endpoints have a large amplitude discontinuity and may click"));
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::audio_editing
