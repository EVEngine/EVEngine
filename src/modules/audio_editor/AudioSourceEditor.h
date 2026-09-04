#pragma once

/**
 * @file AudioSourceEditor.h
 * @brief UI-neutral audio source editor: workspace, waveform, audition, undo.
 */

#include "audio_editing/AudioTarget.h"
#include "audio_editing/AudioTransport.h"
#include "audio_editing/AudioWaveform.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"

#include <cstdint>
#include <memory>
#include <string>

namespace eve::audio {
class Source;
}

namespace eve::sound {
class SoundData;
}

namespace eve::audio_editor {

/**
 * @brief Clock-backed audition used when no live OpenAL Source is attached.
 *
 * Position advances only through @ref advance. Owner-thread only.
 */
class AudioClockTransportBackend final : public audio_editing::IAudioTransportBackend {
public:
    void play() override;
    void pause() override;
    void stop() override;
    [[nodiscard]] audio_editing::EditorResult<void> seek(double seconds) override;
    double tell() const override { return position_; }
    double duration() const override { return duration_; }
    bool   playing() const override { return playing_; }
    void   setNativeLooping(bool) override {}

    /** @brief Install clip length before bind. */
    void setDuration(double seconds);
    /** @brief Advance playhead when playing. */
    void advance(double deltaSeconds);

private:
    double duration_ = 1.0;
    double position_ = 0.0;
    bool   playing_  = false;
};

/**
 * @brief Timeline-style controller for one authored audio source.
 *
 * Owns the document, local undo, waveform cache and audition transport.
 * PCM identity is separate from document revision so volume/pitch edits do not
 * stale-unbind playback. Clip PCM changes increment @ref pcmRevision.
 *
 * @ownership Editor owns the document and clock backend. Live Source is borrowed
 *            from the Audio module when attachLiveAudition succeeds.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class AudioSourceEditor {
public:
    /**
     * @brief Construct a seeded preview source with a generated tone clip.
     * @param targetId Stable editor target identity.
     */
    explicit AudioSourceEditor(std::string targetId);

    AudioSourceEditor(const AudioSourceEditor&)            = delete;
    AudioSourceEditor& operator=(const AudioSourceEditor&) = delete;
    ~AudioSourceEditor();

    /** @brief Borrow the authoritative source document. */
    const audio_editing::AudioSourceTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Sources / Waveform / Inspector / Transport panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] audio_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    /**
     * @brief Rebuild waveform envelopes for the given pixel width.
     * @param width Viewport width in pixels; must be positive and finite.
     */
    [[nodiscard]] audio_editing::EditorResult<void> setViewportWidth(float width);

    /**
     * @brief Seek from a waveform x coordinate in the current viewport.
     * @param x Pixel x in the waveform view.
     */
    [[nodiscard]] audio_editing::EditorResult<void> seekX(float x);

    /** @brief Seek preview to an exact time in seconds. */
    [[nodiscard]] audio_editing::EditorResult<void> seekSeconds(double seconds);

    /** @brief Commit an absolute property assignment through the document transaction path. */
    [[nodiscard]] audio_editing::EditorResult<void> setProperty(const std::string& path,
                                                                const audio_editing::EditorValue& value);

    [[nodiscard]] audio_editing::EditorResult<editor::TransactionReceipt> undo();
    [[nodiscard]] audio_editing::EditorResult<editor::TransactionReceipt> redo();

    [[nodiscard]] audio_editing::EditorResult<void> play();
    [[nodiscard]] audio_editing::EditorResult<void> pause();
    [[nodiscard]] audio_editing::EditorResult<void> stop();

    /**
     * @brief Poll audition and advance the clock backend.
     * @param deltaSeconds Injected frame time.
     */
    [[nodiscard]] audio_editing::EditorResult<audio_editing::AudioTransportSnapshot> update(double deltaSeconds);

    /**
     * @brief Bind a live OpenAL Source generated from the preview PCM when Audio/Sound exist.
     * @return Applied when live audition is bound; Unsupported when modules are absent.
     */
    [[nodiscard]] audio_editing::EditorResult<void> attachLiveAudition();

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    bool          isPlaying() const noexcept;
    std::uint64_t revision() const noexcept { return target_.revision(); }
    std::uint64_t pcmRevision() const noexcept { return pcmRevision_; }
    double        duration() const noexcept;
    double        playhead() const noexcept;
    float         layoutWidth() const noexcept { return viewportWidth_; }
    int           bucketCount() const noexcept;
    float         playheadX() const noexcept;
    float         loopStartX() const noexcept;
    float         loopEndX() const noexcept;

    /** @brief Read a property using the source selection. */
    [[nodiscard]] audio_editing::PropertyReadResult read(const std::string& path) const;

    /** @brief Envelope bucket for drawing; channel 0. */
    [[nodiscard]] const audio_editing::AudioWaveformBucket* bucket(int index) const;

    /** @brief Latest transport observation after a successful update/play. */
    const audio_editing::AudioTransportSnapshot& transport() const noexcept { return lastTransport_; }

    /** @brief Canonical snapshot of the source document. */
    audio_editing::EditorValue snapshot() const { return target_.snapshotValue(); }

private:
    [[nodiscard]] editor::SelectionSnapshot selection() const;
    [[nodiscard]] audio_editing::EditorResult<void> rebuildWaveform();
    [[nodiscard]] audio_editing::EditorResult<void> bindClockAudition();
    [[nodiscard]] audio_editing::EditorResult<void> syncLoop();
    [[nodiscard]] audio_editing::EditorResult<void> publishLive();
    [[nodiscard]] audio_editing::Revision           auditionRevision() const;
    void                                            seedPreviewDocument();
    void                                            generateTonePcm();

    audio_editing::AudioSourceTarget         target_;
    editor::LocalWorldAuthority              authority_;
    editor::LocalTransactionBackend          transactions_;
    audio_editing::AudioWaveformService      waveforms_;
    audio_editing::EditorAudioPcm            pcm_;
    audio_editing::AudioWaveformResult       waveform_;
    AudioClockTransportBackend               clock_;
    audio_editing::AudioAuditionTransport    transport_;
    std::unique_ptr<audio_editing::AudioSourceTransportBackend> liveBackend_;
    audio_editing::AudioTransportSnapshot    lastTransport_;
    std::unique_ptr<sound::SoundData>        soundData_;
    audio::Source*                           liveSource_   = nullptr;
    bool                                     liveBound_    = false;
    std::uint64_t                            pcmRevision_  = 1;
    std::uint64_t                            txSequence_   = 0;
    float                                    viewportWidth_ = 640.0f;
};

}  // namespace eve::audio_editor
