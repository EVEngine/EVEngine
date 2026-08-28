#pragma once

#include "editor/EditorProtocol.h"

#include <string>

namespace eve::audio { class Source; }

namespace eve::editor {

/** @brief Playback state exposed to audio and dialogue editor presenters. */
enum class AudioTransportState { Stopped, Playing, Paused };

/** @brief Immutable revision-tagged audition playhead observation. */
struct AudioTransportSnapshot {
    StableId asset;
    Revision sourceRevision = 0;
    AudioTransportState state = AudioTransportState::Stopped;
    double position = 0.0;
    double duration = 0.0;
    bool loopEnabled = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
};

/** @brief Minimal playback backend boundary used by the deterministic transport controller. */
class IAudioTransportBackend {
public:
    virtual ~IAudioTransportBackend() = default;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual bool seek(double seconds) = 0;
    virtual double tell() const = 0;
    virtual double duration() const = 0;
    virtual bool playing() const = 0;
    virtual void setNativeLooping(bool enabled) = 0;
};

/** @brief Revision-safe play/pause/seek/custom-loop state machine for asset audition. */
class AudioAuditionTransport {
public:
    /** @brief Bind a borrowed backend and stop any previously bound audition. */
    EditorResult<void> bind(StableId asset, Revision sourceRevision,
                            IAudioTransportBackend* backend);
    /** @brief Configure a bounded loop range; zero end uses the clip duration. */
    EditorResult<void> setLoop(Revision expectedRevision, bool enabled,
                               double startSeconds = 0.0, double endSeconds = 0.0);
    EditorResult<void> play(Revision expectedRevision);
    EditorResult<void> pause(Revision expectedRevision);
    EditorResult<void> stop(Revision expectedRevision);
    EditorResult<void> seek(Revision expectedRevision, double seconds);
    /** @brief Poll backend state and wrap the custom loop without changing documents. */
    EditorResult<AudioTransportSnapshot> update(Revision expectedRevision);
    /** @brief Read the current playhead only for the bound source revision. */
    EditorResult<AudioTransportSnapshot> snapshot(Revision expectedRevision) const;
    /** @brief Stop and forget the borrowed backend. */
    void unbind();

private:
    EditorResult<void> validateRevision(Revision expectedRevision) const;
    AudioTransportSnapshot observe() const;
    StableId asset_;
    Revision revision_ = 0;
    IAudioTransportBackend* backend_ = nullptr;
    AudioTransportState state_ = AudioTransportState::Stopped;
    bool loopEnabled_ = false;
    double loopStart_ = 0.0;
    double loopEnd_ = 0.0;
};

/** @brief Non-owning transport backend for a live OpenAL-backed audio Source. */
class AudioSourceTransportBackend final : public IAudioTransportBackend {
public:
    explicit AudioSourceTransportBackend(audio::Source* source) : source_(source) {}
    void play() override;
    void pause() override;
    void stop() override;
    bool seek(double seconds) override;
    double tell() const override;
    double duration() const override;
    bool playing() const override;
    void setNativeLooping(bool enabled) override;
private:
    audio::Source* source_ = nullptr;
};

}  // namespace eve::editor
