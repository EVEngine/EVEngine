#pragma once

#include "audio_editing/AudioEditingTypes.h"

#include <map>
#include <string>
#include <vector>

namespace eve::audio_editing {

/** @brief Serializable audio-source authoring target independent of OpenAL handles. */
class AudioSourceTarget final : public virtual IEditableTarget,
                                public IDomainOperationTarget,
                                public IDomainOperationTargetStaging,
                                public IPropertyProvider {
public:
    explicit AudioSourceTarget(std::string id);

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Capture deterministic source settings for scene/prefab persistence. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a versioned source snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Report missing assets and inconsistent attenuation/loop settings. */
    std::vector<EditorDiagnostic> validate() const;

private:
    static PropertySchema sourceSchema();
    static std::map<std::string, EditorValue> defaults();
    bool selectionMatches(const SelectionSnapshot& selection) const;

    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<std::string, EditorValue> values_;
};

/** @brief Atomic runtime publication boundary for one authored audio source. */
class IAudioSourceRuntimeSink {
public:
    virtual ~IAudioSourceRuntimeSink() = default;
    /**
     * @brief Publish one fully validated authoring candidate.
     * @return Applied on success; failures must leave the live source unchanged.
     */
    virtual EditorResult<void> publish(const AudioSourceTarget& candidate) = 0;
};

/**
 * @brief Operation target coupling an owned audio document to an atomic live sink.
 *
 * Candidate clones never publish. The authoritative instance publishes the
 * complete candidate before replacing its authoring state, so standard
 * transaction undo/redo follows the same live path.
 */
class AudioSourcePublishingTarget final : public IDomainOperationTarget,
                                          public IDomainOperationTargetStaging {
public:
    /** @brief Create an authoring target bound to a non-owning runtime sink. */
    AudioSourcePublishingTarget(std::string id, IAudioSourceRuntimeSink* sink);

    const std::string& targetId() const override { return document_.targetId(); }
    unsigned long long revision() const override { return document_.revision(); }
    EditRegion dirtyRegion() const override { return document_.dirtyRegion(); }
    void clearDirtyRegion() override { document_.clearDirtyRegion(); }
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;

    /** @brief Access the owned authoring document for property inspection/planning. */
    AudioSourceTarget& authoringTarget() { return document_; }
    /** @brief Access the owned authoring document as an immutable snapshot source. */
    const AudioSourceTarget& authoringTarget() const { return document_; }

private:
    AudioSourceTarget document_;
    IAudioSourceRuntimeSink* sink_ = nullptr;
    bool staging_ = false;
};

/** @brief Immutable mixer bus snapshot for hierarchy and routing panels. */
struct AudioBusSnapshot {
    ObjectId id;
    ObjectId parent;
    std::string name;
    double volume = 1.0;
    bool mute = false;
    bool solo = false;
    EditorValue effects = EditorValue::Array{};
};

/** @brief Serializable mixer-bus hierarchy, including master bus. */
class AudioMixerTarget final : public virtual IEditableTarget,
                               public IDomainOperationTarget,
                               public IDomainOperationTargetStaging {
public:
    explicit AudioMixerTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Read one bus snapshot. */
    EditorResult<AudioBusSnapshot> bus(const ObjectId& id) const;
    /** @brief Enumerate direct child buses in stable order. */
    std::vector<ObjectId> children(const ObjectId& parent) const;
    /** @brief Plan a reversible bus creation. */
    EditorResult<DomainOperation> makeCreate(AudioBusSnapshot bus) const;
    /** @brief Plan a reversible leaf-bus deletion; master cannot be deleted. */
    EditorResult<DomainOperation> makeDelete(const ObjectId& id) const;
    /** @brief Plan a reversible bus settings/reparent change. */
    EditorResult<DomainOperation> makeReplace(AudioBusSnapshot bus) const;
    /** @brief Capture deterministic mixer hierarchy content. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load and validate a versioned mixer hierarchy. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    static EditorValue busValue(const AudioBusSnapshot& bus);
    static EditorResult<AudioBusSnapshot> parseBus(const EditorValue& value);
    bool wouldCycle(const ObjectId& id, const ObjectId& parent) const;

    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<ObjectId, AudioBusSnapshot> buses_;
};

}  // namespace eve::audio_editing

namespace eve::audio {
class Source;
}

namespace eve::audio_editing {

/** @brief Optional bridge applying authoring settings to an existing live Source. */
class AudioSourceRuntimeApplier {
public:
    EditorResult<void> apply(const AudioSourceTarget& target, audio::Source* source) const;
};

/** @brief Real Source backend for AudioSourcePublishingTarget. */
class AudioSourceRuntimeSink final : public IAudioSourceRuntimeSink {
public:
    /** @brief Bind a borrowed live Source that must outlive this sink. */
    explicit AudioSourceRuntimeSink(audio::Source* source) : source_(source) {}
    EditorResult<void> publish(const AudioSourceTarget& candidate) override;

private:
    audio::Source* source_ = nullptr;
};

}  // namespace eve::audio_editing
