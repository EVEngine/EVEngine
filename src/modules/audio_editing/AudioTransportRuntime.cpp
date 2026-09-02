#include "audio/Source.h"
#include "audio_editing/AudioTransport.h"

namespace eve::audio_editing {
void AudioSourceTransportBackend::play() {
    if (source_) source_->play();
}
void AudioSourceTransportBackend::pause() {
    if (source_) source_->pause();
}
void AudioSourceTransportBackend::stop() {
    if (source_) source_->stop();
}
EditorResult<void> AudioSourceTransportBackend::seek(double seconds) {
    if (source_ && source_->seek(seconds)) return eve::editing::applied<void>();
    return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.audio.backend-seek"),
                                      "Audio backend rejected seek");
}
double AudioSourceTransportBackend::tell() const { return source_ ? source_->tell() : 0.0; }
double AudioSourceTransportBackend::duration() const { return source_ ? source_->getDuration() : 0.0; }
bool   AudioSourceTransportBackend::playing() const { return source_ && source_->isPlaying(); }
void   AudioSourceTransportBackend::setNativeLooping(bool enabled) {
    if (source_) source_->setLooping(enabled);
}
}  // namespace eve::audio_editing
