#pragma once
#include "common/Export.h"

/**
 * @file AnimationClipEditor.h
 * @brief UI-neutral animation clip editor: workspace, pose scrub, dope sheet, undo.
 */

#include "animation/editing/AnimationClip.h"
#include "animation/editing/SkeletonOverlay.h"
#include "editing/EditingGizmo.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::animation {
class AnimClip;
class AnimSkeleton;
}

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
class EVENGINE_API_EDITORS AnimationClipEditor {
public:
    /** @brief Construct a seeded two-bone preview clip. */
    explicit AnimationClipEditor(std::string targetId);

    /**
     * @brief Atomically replace the editor document from a real runtime skeleton and clip.
     * @param skeleton Borrowed only for this call; the editor copies hierarchy metadata.
     * @param clip Borrowed only for this call; all authored keys are copied.
     * @return Applied, or a structured rejection when the runtime inputs are invalid.
     * @thread Owner thread only. @reentrancy Does not invoke scripts or callbacks.
     */
    [[nodiscard]] animation_editing::Result<void> loadRuntimeClip(const animation::AnimSkeleton& skeleton,
                                                                        const animation::AnimClip& clip);
    /**
     * @brief Replace a runtime clip with the current authoritative editor document.
     * @param clip Runtime clip mutated atomically through AnimClip::adopt.
     * @param skeleton Skeleton used to resolve authored bone names.
     * @return Applied, or a structured error; failure leaves @p clip unchanged.
     * @thread Owner thread only. @reentrancy Does not invoke scripts or callbacks.
     */
    [[nodiscard]] animation_editing::Result<void> writeRuntimeClip(animation::AnimClip& clip,
                                                                         const animation::AnimSkeleton& skeleton) const;

    AnimationClipEditor(const AnimationClipEditor&)            = delete;
    AnimationClipEditor& operator=(const AnimationClipEditor&) = delete;

    /** @brief Borrow the authoritative clip document. */
    const animation_editing::AnimationClipDocumentTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Skeleton / Pose Preview / Inspector / Dope panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] animation_editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    /**
     * @brief Configure dope-sheet pixel layout.
     * @param width Timeline width in pixels.
     * @param rowHeight Track row height.
     * @param labelWidth Left bone-name column width.
     */
    [[nodiscard]] animation_editing::Result<void> setViewport(float width, float rowHeight, float labelWidth);

    [[nodiscard]] animation_editing::Result<void> seekSeconds(double seconds);
    [[nodiscard]] animation_editing::Result<void> seekX(float x);
    [[nodiscard]] animation_editing::Result<void> pointerDown(float x, float y);
    [[nodiscard]] animation_editing::Result<void> selectBone(std::string bone);
    [[nodiscard]] animation_editing::Result<void> setMaskWeight(double weight);
    /**
     * @brief Commit clip duration through the document transaction path.
     * @param duration New duration in seconds; must be positive and finite.
     */
    [[nodiscard]] animation_editing::Result<void> setDuration(double duration);
    /**
     * @brief Commit authored sample rate through the document transaction path.
     * @param sampleRate New sample rate; must be positive and finite.
     */
    [[nodiscard]] animation_editing::Result<void> setSampleRate(double sampleRate);
    /** @brief Commit looping through the document transaction path. */
    [[nodiscard]] animation_editing::Result<void> setLoop(bool loop);
    /** @brief Insert or replace a transform key for the selected bone at the playhead. */
    [[nodiscard]] animation_editing::Result<void> keySelectedBone();
    /** @brief Delete the selected transform key. */
    [[nodiscard]] animation_editing::Result<void> deleteSelectedKey();
    /** @brief Replace the selected bone position at the playhead and create a key when needed. */
    [[nodiscard]] animation_editing::Result<void> setSelectedPosition(double x, double y, double z);
    /** @brief Replace the selected bone Euler rotation in degrees and create a key when needed. */
    [[nodiscard]] animation_editing::Result<void> setSelectedRotation(double xDegrees, double yDegrees,
                                                                            double zDegrees);
    /** @brief Replace the selected bone scale at the playhead and create a key when needed. */
    [[nodiscard]] animation_editing::Result<void> setSelectedScale(double x, double y, double z);
    [[nodiscard]] animation_editing::Result<void> moveSelectedKey(double time);

    [[nodiscard]] animation_editing::Result<editor::TransactionReceipt> undo();
    [[nodiscard]] animation_editing::Result<editor::TransactionReceipt> redo();

    void play() noexcept;
    /** @brief Pause. */
    void pause() noexcept;
    /** @brief Stops . */
    void stop() noexcept;
    /** @brief Advance playhead by injected dt and refresh pose/overlay. */
    [[nodiscard]] animation_editing::Result<void> update(double deltaSeconds);

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    /** @brief Can redo. */
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    /** @brief True when playing. */
    bool          isPlaying() const noexcept { return playing_; }
    /** @brief Revision. */
    std::uint64_t revision() const noexcept { return target_.revision(); }
    /** @brief Duration. */
    double        duration() const noexcept { return target_.duration(); }
    /** @brief Sample rate. */
    double        sampleRate() const noexcept { return target_.sampleRate(); }
    /** @brief True when looping. */
    bool          isLooping() const noexcept { return target_.isLooping(); }
    /** @brief Playhead. */
    double        playhead() const noexcept { return playhead_; }
    /** @brief Layout width. */
    float         layoutWidth() const noexcept { return viewportWidth_; }
    /** @brief Layout height. */
    float         layoutHeight() const noexcept;
    /** @brief Playhead x. */
    float         playheadX() const noexcept;
    /** @brief Selected bone. */
    std::string   selectedBone() const { return selectedBone_; }
    /** @brief Selected mask weight. */
    double        selectedMaskWeight() const;
    /** @brief True when selected key. */
    bool          hasSelectedKey() const noexcept { return !selectedKeyId_.empty(); }
    /** @brief Selected key time. */
    double        selectedKeyTime() const;
    /** @brief Selected position x. */
    double        selectedPositionX() const;
    /** @brief Selected position y. */
    double        selectedPositionY() const;
    /** @brief Selected position z. */
    double        selectedPositionZ() const;
    /** @brief Selected rotation x. */
    double        selectedRotationX() const;
    /** @brief Selected rotation y. */
    double        selectedRotationY() const;
    /** @brief Selected rotation z. */
    double        selectedRotationZ() const;
    /** @brief Selected scale x. */
    double        selectedScaleX() const;
    /** @brief Selected scale y. */
    double        selectedScaleY() const;
    /** @brief Selected scale z. */
    double        selectedScaleZ() const;
    /** @brief Bone count. */
    int           boneCount() const noexcept;
    /** @brief Bone name. */
    std::string   boneName(int index) const;
    /** @brief Bone parent. */
    std::string   boneParent(int index) const;

    /** @brief Track count. */
    int         trackCount() const;
    /** @brief Track bone. */
    std::string trackBone(int index) const;
    /** @brief Track id. */
    std::string trackId(int index) const;
    /** @brief True when track selected. */
    bool        isTrackSelected(int index) const;
    /** @brief Key count. */
    int         keyCount() const;
    /** @brief Key x. */
    float       keyX(int index) const;
    /** @brief Key y. */
    float       keyY(int index) const;
    /** @brief True when key selected. */
    bool        isKeySelected(int index) const;
    /** @brief Event count. */
    int         eventCount() const;
    /** @brief Event x. */
    float       eventX(int index) const;
    /** @brief Event name. */
    std::string eventName(int index) const;

    /** @brief Primitive count. */
    int         primitiveCount() const;
    /** @brief Primitive kind. */
    std::string primitiveKind(int index) const;
    /** @brief Primitive x. */
    float       primitiveX(int index) const;
    /** @brief Primitive y. */
    float       primitiveY(int index) const;
    /** @brief Primitive dir x. */
    float       primitiveDirX(int index) const;
    /** @brief Primitive dir y. */
    float       primitiveDirY(int index) const;
    /** @brief Primitive length. */
    float       primitiveLength(int index) const;
    /** @brief Primitive radius. */
    float       primitiveRadius(int index) const;
    /** @brief Primitive r. */
    float       primitiveR(int index) const;
    /** @brief Primitive g. */
    float       primitiveG(int index) const;
    /** @brief Primitive b. */
    float       primitiveB(int index) const;

    /** @brief Preview. */
    const animation_editing::AnimationClipPreview& preview() const noexcept { return preview_; }

private:
    struct SkeletonBone {
        std::string name;
        std::string parent;
    };
    struct TimelineKey {
        animation_editing::StableId trackId;
        std::string                 bone;
        int                         keyIndex = 0;
        double                      time     = 0.0;
        int                         row      = 0;
    };

    [[nodiscard]] animation_editing::Result<void> commit(
        animation_editing::Result<animation_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] animation_editing::Result<void> refreshPreview();
    [[nodiscard]] animation_editing::Result<void> updateSelectedTransform(
        const animation_editing::AnimationTransformKey& value, std::string label);
    [[nodiscard]] animation_editing::AnimationTransformKey sampledSelectedTransform() const;
    void                                                seedPreviewClip();
    std::vector<std::string>                            skeletonBones() const;
    std::string                                         skeletonParent(const std::string& bone) const;
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
    animation_editing::StableId                    selectedKeyId_;
    std::uint64_t                                  txSequence_       = 0;
    double                                         playhead_         = 0.0;
    bool                                           playing_          = false;
    float                                          viewportWidth_    = 800.0f;
    float                                          rowHeight_        = 36.0f;
    float                                          labelWidth_       = 120.0f;
    std::vector<SkeletonBone>                      skeleton_;
};

}  // namespace eve::animation_editor
