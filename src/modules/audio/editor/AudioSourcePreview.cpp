#include "audio/editor/AudioSourcePreview.h"
#include "audio/editor/AudioSourceEditor.h"
#include "editor/presentation/AsyncPreview.h"

#include <cmath>

namespace eve::audio_editor {
namespace {
class NativeWaveformProvider final : public IAudioSourcePreviewProvider {
public:
    audio_editing::EditorResult<audio_editing::AudioWaveformResult> generate(
        const audio_editing::AudioWaveformRequest& request, const audio_editing::EditorAudioPcm& pcm,
        const editing::TaskContext& context) const override {
        if (context.isCancellationRequested())
            return editing::failed<audio_editing::AudioWaveformResult>(
                editing::Status::Cancelled, editing::RuleId("editor.audio.preview-cancelled"),
                "Waveform request cancelled");
        auto result = audio_editing::AudioWaveformService{}.generate(request, pcm);
        if (result.status != editing::Status::Applied)
            return editing::failed<audio_editing::AudioWaveformResult>(
                result.status, editing::RuleId("editor.audio.waveform"), "Could not generate waveform");
        return editing::applied(std::move(result));
    }
};
struct Publication {
    audio_editing::AudioWaveformResult waveform;
    std::uint64_t                      revision = 0;
    float                              width    = 0;
};
}  // namespace
std::shared_ptr<const IAudioSourcePreviewProvider> makeAudioSourcePreviewProvider() {
    return std::make_shared<NativeWaveformProvider>();
}
struct AudioSourceEditor::PreviewSession {
    editor::AsyncPreview<Publication>                  async;
    std::shared_ptr<const IAudioSourcePreviewProvider> provider = makeAudioSourcePreviewProvider();
};
void                 AudioSourceEditor::PreviewSessionDeleter::operator()(PreviewSession* value) const { delete value; }
void                 AudioSourceEditor::initializePreview() { previewSession_.reset(new PreviewSession); }
editor::PreviewState AudioSourceEditor::previewState() const noexcept { return previewSession_->async.state(); }
audio_editing::EditorResult<void> AudioSourceEditor::setPreviewProvider(
    std::shared_ptr<const IAudioSourcePreviewProvider> provider) {
    auto changed = previewSession_->async.replaceProvider(provider != nullptr);
    if (!changed.ok()) return changed;
    previewSession_->provider = std::move(provider);
    return editing::applied<void>();
}
audio_editing::EditorResult<void> AudioSourceEditor::cancelPreview() { return previewSession_->async.cancel(); }
void                              AudioSourceEditor::closePreview() {
    previewSession_->async.close();
    previewSession_->provider.reset();
}

audio_editing::EditorResult<void> AudioSourceEditor::requestPreview() {
    auto provider = previewSession_->provider;
    if (!provider)
        return editing::failed<void>(editing::Status::Unsupported, editing::RuleId("editor.audio.preview-provider"),
                                     "Waveform provider is unavailable");
    audio_editing::AudioWaveformRequest request;
    request.asset          = "asset://preview/tone.sine";
    request.sourceRevision = auditionRevision();
    request.pixelWidth     = static_cast<int>(std::lround(requestedViewportWidth_));
    auto readLoop          = [this](const char* path) -> editing::Result<double> {
        const auto property = read(path);
        if (const auto* value = property.value.getIf<double>()) return editing::applied(*value);
        if (const auto* value = property.value.getIf<std::int64_t>())
            return editing::applied(static_cast<double>(*value));
        return editing::rejected<double>(editing::RuleId("editor.audio.preview-loop"), "Loop property is not numeric");
    };
    auto start = readLoop("play.loop-start");
    if (!start.ok()) return editing::Result<void>::failure(start.status());
    auto end = readLoop("play.loop-end");
    if (!end.ok()) return editing::Result<void>::failure(end.status());
    request.loopStart   = start.value();
    request.loopEnd     = end.value();
    auto       pcm      = pcm_;
    const auto revision = target_.revision();
    const auto width    = requestedViewportWidth_;
    return previewSession_->async.request([provider = std::move(provider), request = std::move(request),
                                           pcm = std::move(pcm), revision,
                                           width](const editing::TaskContext& context) -> editing::Result<Publication> {
        auto result = provider->generate(request, pcm, context);
        if (!result.ok()) return editing::Result<Publication>::failure(result.status());
        return editing::applied(Publication{std::move(result).takeValue(), revision, width});
    });
}
audio_editing::EditorResult<void> AudioSourceEditor::pollPreview() {
    auto result = previewSession_->async.poll();
    if (!result) return previewState().phase == editor::PreviewPhase::Pending ? editing::pending() : editing::noOp();
    if (!result->ok()) return editing::Result<void>::failure(result->status());
    auto publication = std::move(*result).takeValue();
    if (publication.revision != revision() || publication.waveform.sourceRevision != auditionRevision() ||
        publication.width != requestedViewportWidth_ || publication.waveform.asset != "asset://preview/tone.sine" ||
        publication.waveform.status != editing::Status::Applied) {
        previewSession_->async.rejectPublication();
        return editing::failed<void>(editing::Status::Conflict, editing::RuleId("editor.audio.stale-preview"),
                                     "Waveform inputs changed before publication");
    }
    waveform_      = std::move(publication.waveform);
    viewportWidth_ = publication.width;
    return editing::applied<void>();
}
}  // namespace eve::audio_editor
