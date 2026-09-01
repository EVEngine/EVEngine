#include "audio_editing/AudioImportDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::audio_editing {

AudioImportInspectionResult AudioImportDiagnosticsService::inspect(
    const AudioImportInspectionRequest& request, const EditorAudioPcm& pcm) const {
    AudioImportInspectionResult result;
    result.asset = request.asset; result.sourceRevision = request.sourceRevision;
    if (request.asset.empty() || request.sourceRevision == 0 || request.codec.empty() ||
        pcm.sampleRate < 8000 || pcm.sampleRate > 384000 || pcm.channels < 1 || pcm.channels > 8 ||
        pcm.samples.empty() || pcm.samples.size() % static_cast<std::size_t>(pcm.channels) != 0 ||
        pcm.samples.size() > request.maximumSamples || request.maximumSamples == 0 ||
        !std::isfinite(request.clippingThreshold) || request.clippingThreshold <= 0.0 ||
        request.clippingThreshold > 1.0 || !std::isfinite(request.silenceThreshold) ||
        request.silenceThreshold < 0.0 || request.silenceThreshold >= request.clippingThreshold) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.audio.invalid-import-inspection"),
            DiagnosticSeverity::Error, "Audio inspection requires valid identity, format, thresholds and bounded PCM"});
        return result;
    }
    const std::size_t frames = pcm.samples.size() / static_cast<std::size_t>(pcm.channels);
    result.duration = static_cast<double>(frames) / static_cast<double>(pcm.sampleRate);
    if (pcm.samples.size() > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.audio.decoded-size-overflow"), DiagnosticSeverity::Error,
                                      "Decoded audio size exceeds the host range"});
        return result;
    }
    result.decodedBytes = pcm.samples.size() * sizeof(float);
    result.compressionRatio = request.encodedBytes > 0
        ? static_cast<double>(result.decodedBytes) / static_cast<double>(request.encodedBytes) : 0.0;
    result.recommendStreaming = result.decodedBytes > request.staticMemoryBudget || result.duration > 10.0;
    result.channels.resize(static_cast<std::size_t>(pcm.channels));
    std::vector<long double> squares(result.channels.size(), 0.0L);
    std::vector<long double> sums(result.channels.size(), 0.0L);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < pcm.channels; ++channel) {
            const float sample = pcm.samples[frame * static_cast<std::size_t>(pcm.channels) +
                                             static_cast<std::size_t>(channel)];
            if (!std::isfinite(sample)) {
                result.status = EditorStatus::Rejected;
                result.diagnostics.push_back({RuleId("editor.audio.nonfinite-sample"), DiagnosticSeverity::Error,
                                              "Decoded PCM contains a non-finite sample"});
                return result;
            }
            auto& statistics = result.channels[static_cast<std::size_t>(channel)];
            const double absolute = std::abs(static_cast<double>(sample));
            statistics.peak = std::max(statistics.peak, absolute);
            if (absolute >= request.clippingThreshold) ++statistics.clippedSamples;
            sums[static_cast<std::size_t>(channel)] += sample;
            squares[static_cast<std::size_t>(channel)] += static_cast<long double>(sample) * sample;
        }
    }
    bool silent = true;
    for (std::size_t channel = 0; channel < result.channels.size(); ++channel) {
        auto& statistics = result.channels[channel];
        statistics.dcOffset = static_cast<double>(sums[channel] / frames);
        statistics.rms = std::sqrt(static_cast<double>(squares[channel] / frames));
        silent = silent && statistics.peak <= request.silenceThreshold;
        if (statistics.clippedSamples > 0)
            result.diagnostics.push_back({RuleId("editor.audio.clipping"), DiagnosticSeverity::Warning,
                "Audio channel " + std::to_string(channel) + " contains clipped samples"});
        if (std::abs(statistics.dcOffset) > 0.01)
            result.diagnostics.push_back({RuleId("editor.audio.dc-offset"), DiagnosticSeverity::Warning,
                "Audio channel " + std::to_string(channel) + " has significant DC offset"});
    }
    if (silent) result.diagnostics.push_back({RuleId("editor.audio.silence"), DiagnosticSeverity::Warning,
                                              "Decoded audio is effectively silent"});
    if (result.recommendStreaming && !request.streaming)
        result.diagnostics.push_back({RuleId("editor.audio.streaming-recommended"), DiagnosticSeverity::Warning,
                                      "Long or memory-heavy audio should use streaming playback"});
    if (!result.recommendStreaming && request.streaming)
        result.diagnostics.push_back({RuleId("editor.audio.static-recommended"), DiagnosticSeverity::Info,
                                      "Short audio can avoid streaming overhead"});
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::audio_editing
