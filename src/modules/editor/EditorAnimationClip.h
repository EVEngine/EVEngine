#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTargetV2.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::animation {
class AnimClip;
class AnimSkeleton;
}

namespace eve::editor {

/** @brief One stable transform key used by the animation timeline. */
struct AnimationTransformKey {
    StableId id;
    double time = 0.0;
    double positionX = 0.0, positionY = 0.0, positionZ = 0.0;
    double rotationX = 0.0, rotationY = 0.0, rotationZ = 0.0, rotationW = 1.0;
    double scaleX = 1.0, scaleY = 1.0, scaleZ = 1.0;
};

/** @brief Stable bone track addressed by skeleton bone name. */
struct AnimationBoneTrack {
    StableId id;
    std::string bone;
    std::vector<AnimationTransformKey> keys;
};

/** @brief Stable timeline event marker with optional payload. */
struct AnimationEventRecord {
    StableId id;
    double time = 0.0;
    std::string name;
    std::string payload;
};

/** @brief Per-bone blend mask entry in normalized [0, 1] weight space. */
struct AnimationMaskEntry {
    std::string bone;
    double weight = 1.0;
};

/** @brief Sampled local transform returned by non-destructive timeline scrubbing. */
struct AnimationSampledBone {
    std::string bone;
    double positionX = 0.0, positionY = 0.0, positionZ = 0.0;
    double rotationX = 0.0, rotationY = 0.0, rotationZ = 0.0, rotationW = 1.0;
    double scaleX = 1.0, scaleY = 1.0, scaleZ = 1.0;
    double maskWeight = 1.0;
};

/** @brief Immutable timeline scrub result tied to one document revision. */
struct AnimationClipPreview {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    double time = 0.0;
    std::vector<AnimationSampledBone> bones;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Non-destructive source-to-target skeleton mapping preview. */
struct AnimationRetargetPreview {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    std::map<std::string, std::string> mapping;
    std::vector<std::string> unmatchedSourceBones;
    std::vector<std::string> unmatchedTargetBones;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Timeline editing capability for skeletal clips. */
class IAnimationClipEditTarget {
public:
    virtual ~IAnimationClipEditTarget() = default;
    /** @brief Stable capability id for clip timeline operations. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.animation-clip"); }
    /** @brief Plan replacement of duration, sample rate and looping settings. */
    virtual EditorResult<DomainOperation> makeSetSettings(double duration, double sampleRate, bool loop) const = 0;
    /** @brief Plan creation or replacement of a complete stable bone track. */
    virtual EditorResult<DomainOperation> makeSetTrack(const AnimationBoneTrack& track) const = 0;
    /** @brief Plan removal of a stable bone track. */
    virtual EditorResult<DomainOperation> makeDeleteTrack(const StableId& track) const = 0;
    /** @brief Plan creation or replacement of an event marker. */
    virtual EditorResult<DomainOperation> makeSetEvent(const AnimationEventRecord& event) const = 0;
    /** @brief Plan removal of an event marker. */
    virtual EditorResult<DomainOperation> makeDeleteEvent(const StableId& event) const = 0;
    /** @brief Plan replacement of a bone mask weight. */
    virtual EditorResult<DomainOperation> makeSetMask(const AnimationMaskEntry& mask) const = 0;
};

/** @brief UI-neutral animation clip document with reversible stable-id timeline edits. */
class AnimationClipDocumentTarget final : public IEditableTargetV2,
                                          public IDomainOperationTarget,
                                          public IDomainOperationTargetStaging,
                                          public IAnimationClipEditTarget {
public:
    explicit AnimationClipDocumentTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    EditorResult<DomainOperation> makeSetSettings(double duration, double sampleRate, bool loop) const override;
    EditorResult<DomainOperation> makeSetTrack(const AnimationBoneTrack& track) const override;
    EditorResult<DomainOperation> makeDeleteTrack(const StableId& track) const override;
    EditorResult<DomainOperation> makeSetEvent(const AnimationEventRecord& event) const override;
    EditorResult<DomainOperation> makeDeleteEvent(const StableId& event) const override;
    EditorResult<DomainOperation> makeSetMask(const AnimationMaskEntry& mask) const override;

    /** @brief Return tracks ordered by stable id. */
    std::vector<AnimationBoneTrack> tracks() const;
    /** @brief Return event markers ordered by time then stable id. */
    std::vector<AnimationEventRecord> events() const;
    /** @brief Validate timeline, references and mask against optional target bone names. */
    std::vector<EditorDiagnostic> validate(const std::vector<std::string>& skeletonBones = {}) const;
    /** @brief Sample the authored clip without mutating a runtime asset. */
    AnimationClipPreview preview(double time, const std::vector<std::string>& skeletonBones = {}) const;
    /** @brief Preview exact then normalized-name retarget mapping without baking a clip. */
    AnimationRetargetPreview previewRetarget(const std::vector<std::string>& targetBones) const;
    /** @brief Capture deterministic schema-version-one clip data. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load deterministic schema-version-one clip data. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    double duration_ = 1.0;
    double sampleRate_ = 30.0;
    bool loop_ = true;
    std::map<StableId, AnimationBoneTrack> tracks_;
    std::map<StableId, AnimationEventRecord> events_;
    std::map<std::string, double> mask_;
};

/** @brief Optional bridge producing a real runtime AnimClip from an editor document. */
class AnimationClipRuntimeBuilder {
public:
    /** @brief Build a new runtime clip; caller owns the result. */
    EditorResult<animation::AnimClip*> build(const AnimationClipDocumentTarget& document,
                                             const animation::AnimSkeleton* skeleton) const;
};

}  // namespace eve::editor
