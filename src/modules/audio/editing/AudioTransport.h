#pragma once
#include "common/Export.h"


#include "audio/editing/AudioEditingTypes.h"

#include <string>

namespace eve::audio { class Source; }

namespace eve::audio_editing {

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
    /** @brief Releases IAudioTransportBackend resources. */
    virtual ~IAudioTransportBackend() = default;
    /** @brief Play. */
    virtual void play() = 0;
    /** @brief Pause. */
    virtual void pause() = 0;
    /** @brief Stops . */
    virtual void stop() = 0;
    /** @brief Seek the backend playhead. @return Structured failure when the backend rejects the position. */
    virtual EditorResult<void> seek(double seconds) = 0;
    /** @brief Tell. */
    virtual double tell() const = 0;
    /** @brief Duration. */
    virtual double duration() const = 0;
    /** @brief Playing. */
    virtual bool playing() const = 0;
    /** @brief Sets the native looping. */
    virtual void setNativeLooping(bool enabled) = 0;
};

/** @brief Revision-safe play/pause/seek/custom-loop state machine for asset audition. */
class EVENGINE_API_BACKENDS AudioAuditionTransport {
public:
    /** @brief Bind a borrowed backend and stop any previously bound audition. */
    EditorResult<void> bind(StableId asset, Revision sourceRevision,
                            IAudioTransportBackend* backend);
    /** @brief Configure a bounded loop range; zero end uses the clip duration. */
    EditorResult<void> setLoop(Revision expectedRevision, bool enabled,
                               double startSeconds = 0.0, double endSeconds = 0.0);
    /** @brief Play. */
    EditorResult<void> play(Revision expectedRevision);
    /** @brief Pause. */
    EditorResult<void> pause(Revision expectedRevision);
    /** @brief Stops . */
    EditorResult<void> stop(Revision expectedRevision);
    /** @brief Seeks . */
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
class EVENGINE_API_BACKENDS AudioSourceTransportBackend final : public IAudioTransportBackend {
public:
    /** @brief Audio source transport backend. */
    explicit AudioSourceTransportBackend(audio::Source* source) : source_(source) {}
    /** @brief Play. */
    void play() override;
    /** @brief Pause. */
    void pause() override;
    /** @brief Stops . */
    void stop() override;
    /** @brief Seeks . */
    EditorResult<void> seek(double seconds) override;
    /** @brief Tell. */
    double tell() const override;
    /** @brief Duration. */
    double duration() const override;
    /** @brief Playing. */
    bool playing() const override;
    /** @brief Sets the native looping. */
    void setNativeLooping(bool enabled) override;
private:
    audio::Source* source_ = nullptr;
};

}  // namespace eve::audio_editing
