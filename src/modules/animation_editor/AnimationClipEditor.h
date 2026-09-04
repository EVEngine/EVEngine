#pragma once

/**
 * @file AnimationClipEditor.h
 * @brief UI-neutral animation clip editor: workspace, pose scrub, dope sheet, undo.
 */

#include "animation_editing/AnimationClip.h"
#include "animation_editing/SkeletonOverlay.h"
#include "editing/EditingGizmo.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::animation_editor {

/**
 * @brief Timeline-style controller for one authored animation clip.
 *
 * Owns the clip document, local undo, deterministic scrub preview and a
 * renderer-neutral skeleton overlay. Script presenters draw layout getters only.
 *
 * @ownership Editor owns the document. Overlay snapshots are copied values.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class AnimationClipEditor {
public:
    /** @brief Construct a seeded two-bone preview clip. */
    explicit AnimationClipEditor(std::string targetId);

    AnimationClipEditor(const AnimationClipEditor&)            = delete;
    AnimationClipEditor& operator=(const AnimationClipEditor&) = delete;

    /** @brief Borrow the authoritative clip document. */
    const animation_editing::AnimationClipDocumentTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Skeleton / Pose Preview / Inspector / Dope panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] animation_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    /**
     * @brief Configure dope-sheet pixel layout.
     * @param width Timeline width in pixels.
     * @param rowHeight Track row height.
     * @param labelWidth Left bone-name column width.
     */
    [[nodiscard]] animation_editing::EditorResult<void> setViewport(float width, float rowHeight, float labelWidth);

    [[nodiscard]] animation_editing::EditorResult<void> seekSeconds(double seconds);
    [[nodiscard]] animation_editing::EditorResult<void> seekX(float x);
    [[nodiscard]] animation_editing::EditorResult<void> pointerDown(float x, float y);
    [[nodiscard]] animation_editing::EditorResult<void> selectBone(std::string bone);
    [[nodiscard]] animation_editing::EditorResult<void> setMaskWeight(double weight);
    /**
     * @brief Commit clip duration through the document transaction path.
     * @param duration New duration in seconds; must be positive and finite.
     */
    [[nodiscard]] animation_editing::EditorResult<void> setDuration(double duration);
    /**
     * @brief Commit authored sample rate through the document transaction path.
     * @param sampleRate New sample rate; must be positive and finite.
     */
    [[nodiscard]] animation_editing::EditorResult<void> setSampleRate(double sampleRate);
    /** @brief Commit looping through the document transaction path. */
    [[nodiscard]] animation_editing::EditorResult<void> setLoop(bool loop);
    [[nodiscard]] animation_editing::EditorResult<void> moveSelectedKey(double time);

    [[nodiscard]] animation_editing::EditorResult<editor::TransactionReceipt> undo();
    [[nodiscard]] animation_editing::EditorResult<editor::TransactionReceipt> redo();

    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    /** @brief Advance playhead by injected dt and refresh pose/overlay. */
    [[nodiscard]] animation_editing::EditorResult<void> update(double deltaSeconds);

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    bool          isPlaying() const noexcept { return playing_; }
    std::uint64_t revision() const noexcept { return target_.revision(); }
    double        duration() const noexcept { return target_.duration(); }
    double        sampleRate() const noexcept { return target_.sampleRate(); }
    bool          isLooping() const noexcept { return target_.isLooping(); }
    double        playhead() const noexcept { return playhead_; }
    float         layoutWidth() const noexcept { return viewportWidth_; }
    float         layoutHeight() const noexcept;
    float         playheadX() const noexcept;
    std::string   selectedBone() const { return selectedBone_; }
    double        selectedMaskWeight() const;

    int         trackCount() const;
    std::string trackBone(int index) const;
    std::string trackId(int index) const;
    bool        isTrackSelected(int index) const;
    int         keyCount() const;
    float       keyX(int index) const;
    float       keyY(int index) const;
    bool        isKeySelected(int index) const;
    int         eventCount() const;
    float       eventX(int index) const;
    std::string eventName(int index) const;

    int         primitiveCount() const;
    std::string primitiveKind(int index) const;
    float       primitiveX(int index) const;
    float       primitiveY(int index) const;
    float       primitiveDirX(int index) const;
    float       primitiveDirY(int index) const;
    float       primitiveLength(int index) const;
    float       primitiveRadius(int index) const;
    float       primitiveR(int index) const;
    float       primitiveG(int index) const;
    float       primitiveB(int index) const;

    const animation_editing::AnimationClipPreview& preview() const noexcept { return preview_; }

private:
    struct TimelineKey {
        animation_editing::StableId trackId;
        std::string                 bone;
        int                         keyIndex = 0;
        double                      time     = 0.0;
        int                         row      = 0;
    };

    [[nodiscard]] animation_editing::EditorResult<void> commit(
        animation_editing::EditorResult<animation_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] animation_editing::EditorResult<void> refreshPreview();
    void                                                seedPreviewClip();
    std::vector<std::string>                            skeletonBones() const;
    float                                               timeToX(double time) const;
    double                                              xToTime(float x) const;
    /** @ownership Borrowed overlay primitive owned by this editor. @lifetime Valid until the next preview refresh or destruction; null when index is out of range. */
    const eve::editing::GizmoPrimitive*                 primitiveAt(int index) const;
    std::vector<TimelineKey>                            flattenKeys() const;

    animation_editing::AnimationClipDocumentTarget target_;
    editor::LocalWorldAuthority                    authority_;
    editor::LocalTransactionBackend                transactions_;
    animation_editing::SkeletonOverlayBuilder      overlayBuilder_;
    animation_editing::AnimationClipPreview        preview_;
    eve::editing::GizmoSnapshot                    overlay_;
    std::string                                    selectedBone_ = "Hips";
    animation_editing::StableId                    selectedKeyTrack_;
    int                                            selectedKeyIndex_ = -1;
    std::uint64_t                                  txSequence_       = 0;
    double                                         playhead_         = 0.0;
    bool                                           playing_          = false;
    float                                          viewportWidth_    = 800.0f;
    float                                          rowHeight_        = 36.0f;
    float                                          labelWidth_       = 120.0f;
};

}  // namespace eve::animation_editor
