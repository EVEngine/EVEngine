#include "audio_editing/AudioTransport.h"

#include <algorithm>
#include <cmath>

namespace eve::audio_editing {
namespace {
template <class T>
EditorResult<T> transportError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}
}  // namespace

EditorResult<void> AudioAuditionTransport::bind(StableId asset, Revision revision, IAudioTransportBackend* backend) {
    if (asset.empty() || revision == 0 || !backend)
        return transportError<void>(EditorStatus::Rejected, "editor.audio.transport-bind",
                                    "Audition requires asset identity, revision and playback backend");
    const double duration = backend->duration();
    if (!std::isfinite(duration) || duration <= 0.0)
        return transportError<void>(EditorStatus::Rejected, "editor.audio.transport-duration",
                                    "Audition source requires a finite positive duration");
    unbind();
    asset_    = std::move(asset);
    revision_ = revision;
    backend_  = backend;
    backend_->setNativeLooping(false);
    return eve::editing::applied<void>();
}
EditorResult<void> AudioAuditionTransport::validateRevision(Revision expected) const {
    if (!backend_)
        return transportError<void>(EditorStatus::NotFound, "editor.audio.transport-unbound",
                                    "No audition source is bound");
    if (expected != revision_)
        return transportError<void>(EditorStatus::Conflict, "editor.audio.transport-stale",
                                    "Audition source revision is stale");
    return eve::editing::applied<void>();
}
EditorResult<void> AudioAuditionTransport::setLoop(Revision expected, bool enabled, double start,
                                                   double end) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    const double duration = backend_->duration();
    if (end == 0.0) end = duration;
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 || end <= start || end > duration)
        return transportError<void>(EditorStatus::Rejected, "editor.audio.transport-loop-range",
                                    "Loop range must be finite, ordered and inside the clip");
    loopEnabled_ = enabled;
    loopStart_   = start;
    loopEnd_     = end;
    backend_->setNativeLooping(false);
    return eve::editing::applied<void>();
}
EditorResult<void> AudioAuditionTransport::play(Revision expected) {
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
EditorResult<void> AudioAuditionTransport::pause(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    if (state_ != AudioTransportState::Playing) {
        return eve::editing::noOp();
    }
    backend_->pause();
    state_ = AudioTransportState::Paused;
    return eve::editing::applied<void>();
}
EditorResult<void> AudioAuditionTransport::stop(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    if (state_ == AudioTransportState::Stopped) {
        return eve::editing::noOp();
    }
    backend_->stop();
    state_ = AudioTransportState::Stopped;
    return eve::editing::applied<void>();
}
EditorResult<void> AudioAuditionTransport::seek(Revision expected, double seconds) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) return valid;
    const double duration = backend_->duration();
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > duration)
        return transportError<void>(EditorStatus::Rejected, "editor.audio.transport-seek-range",
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
EditorResult<AudioTransportSnapshot> AudioAuditionTransport::snapshot(Revision expected) const {
    auto valid = validateRevision(expected);
    if (!valid.ok())
        return transportError<AudioTransportSnapshot>(valid.code(), "editor.audio.transport-stale",
                                                      "Audition source is absent or stale");
    return eve::editing::applied<AudioTransportSnapshot>(observe());
}
EditorResult<AudioTransportSnapshot> AudioAuditionTransport::update(Revision expected) {
    auto valid = validateRevision(expected);
    if (!valid.ok()) {
        const EditorStatus status = valid.code();
        if (status == EditorStatus::Conflict) unbind();
        return transportError<AudioTransportSnapshot>(status, "editor.audio.transport-stale",
                                                      "Audition source is absent or stale");
    }
    if (state_ == AudioTransportState::Playing && loopEnabled_ && backend_->tell() >= loopEnd_) {
        auto seekResult = backend_->seek(loopStart_);
        if (!seekResult.ok())
            return transportError<AudioTransportSnapshot>(seekResult.code(),
                                                          "editor.audio.transport-loop-seek",
                                                          "Playback backend cannot wrap the loop");
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
