#include "dialogue_editor/EditorDialoguePreview.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace eve::editor {
namespace {

std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string word, line;
    while (stream >> word) {
        if (line.empty()) line = word;
        else if (static_cast<int>(line.size() + 1 + word.size()) <= width) line += " " + word;
        else { lines.push_back(std::move(line)); line = word; }
        while (static_cast<int>(line.size()) > width) {
            lines.push_back(line.substr(0, static_cast<std::size_t>(width)));
            line.erase(0, static_cast<std::size_t>(width));
        }
    }
    if (!line.empty()) lines.push_back(std::move(line));
    return lines;
}

}  // namespace

DialogueSubtitlePreview DialogueSubtitlePreviewService::build(
    const DialogueSubtitleRequest& request, const AudioWaveformResult* waveform) const {
    DialogueSubtitlePreview result;
    result.lineId = request.lineId; result.sourceRevision = request.sourceRevision;
    const double values[]{request.viewportWidth, request.viewportHeight, request.safeMarginX,
                          request.safeMarginY, request.minimumDuration,
                          request.charactersPerSecond, request.voiceDuration};
    if (request.lineId.empty() || request.locale.empty() || request.text.empty() ||
        request.maximumCharactersPerLine <= 0 || request.maximumLines <= 0 ||
        std::any_of(std::begin(values), std::end(values), [](double value) { return !std::isfinite(value); }) ||
        request.viewportWidth <= 0.0 || request.viewportHeight <= 0.0 ||
        request.safeMarginX < 0.0 || request.safeMarginX >= 0.5 ||
        request.safeMarginY < 0.0 || request.safeMarginY >= 0.5 ||
        request.minimumDuration < 0.0 || request.charactersPerSecond <= 0.0 || request.voiceDuration < 0.0) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            DiagnosticCode::InvalidArgument, RuleId("editor.dialogue.invalid-subtitle-request"),
            DiagnosticSeverity::Error,
            "Subtitle preview requires valid identity, text, viewport and timing constraints"));
        return result;
    }
    result.safeX = request.viewportWidth * request.safeMarginX;
    result.safeY = request.viewportHeight * request.safeMarginY;
    result.safeWidth = request.viewportWidth - result.safeX * 2.0;
    result.safeHeight = request.viewportHeight - result.safeY * 2.0;
    result.lines = wrap(request.text, request.maximumCharactersPerLine);
    result.estimatedReadDuration = std::max(request.minimumDuration,
        static_cast<double>(request.text.size()) / request.charactersPerSecond);
    if (static_cast<int>(result.lines.size()) > request.maximumLines)
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            DiagnosticCode::PreconditionViolation, RuleId("editor.dialogue.subtitle-line-overflow"),
            DiagnosticSeverity::Error, "Subtitle exceeds the configured maximum line count"));
    if (request.voiceDuration > 0.0 && request.voiceDuration + 0.25 < result.estimatedReadDuration)
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            DiagnosticCode::PreconditionViolation, RuleId("editor.dialogue.voice-too-short"),
            DiagnosticSeverity::Warning,
            "Localized voice is shorter than the estimated reading duration"));
    if (waveform) {
        if (waveform->status != EditorStatus::Applied || waveform->sourceRevision != request.sourceRevision) {
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                DiagnosticCode::PreconditionViolation, RuleId("editor.dialogue.stale-voice-waveform"),
                DiagnosticSeverity::Warning,
                "Voice waveform is unavailable or belongs to another source revision"));
        } else if (!waveform->envelopes.empty()) {
            const auto& channel = waveform->envelopes.front();
            result.lipEnvelope.reserve(channel.size());
            for (const AudioWaveformBucket& bucket : channel)
                result.lipEnvelope.push_back({(bucket.startTime + bucket.endTime) * 0.5,
                                              std::clamp(static_cast<double>(bucket.rms), 0.0, 1.0)});
        }
    }
    const bool hasError = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const EditorDiagnostic& diagnostic) {
            return diagnostic.severity() == DiagnosticSeverity::Error;
        });
    result.status = hasError ? EditorStatus::Failed : EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor
