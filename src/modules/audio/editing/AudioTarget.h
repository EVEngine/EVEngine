#pragma once
#include "common/Export.h"


#include "audio/editing/AudioEditingTypes.h"

#include <map>
#include <string>
#include <vector>

namespace eve::audio_editing {

/** @brief Serializable audio-source authoring target independent of OpenAL handles. */
class EVENGINE_API_BACKENDS AudioSourceTarget final : public ::eve::editing::EditableTargetState,
                                                      public virtual IEditableTarget,
                                                      public IDomainOperationTarget,
                                                      public IDomainOperationTargetStaging,
                                                      public IPropertyProvider,
                                                      public IEditingSnapshotProvider {
public:
    /** @brief Stable capability identity for audio-source property editing. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editor.target.audio-source"); }

    /** @brief Audio source target. */
    explicit AudioSourceTarget(std::string id);

    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads . */
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Capture deterministic source settings for scene/prefab persistence. */
    EditorValue snapshotValue() const override;
    /** @brief Atomically load a versioned source snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Report missing assets and inconsistent attenuation/loop settings. */
    std::vector<EditorDiagnostic> validate() const;

private:
    /** @brief Source schema. */
    static PropertySchema sourceSchema();
    static std::map<std::string, EditorValue> defaults();
    /** @brief Selection matches. */
    bool selectionMatches(const SelectionSnapshot& selection) const;

    std::string                        id_;
    std::map<std::string, EditorValue> values_;
};

/** @brief Atomic runtime publication boundary for one authored audio source. */
class IAudioSourceRuntimeSink {
public:
    /** @brief Releases IAudioSourceRuntimeSink resources. */
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
class EVENGINE_API_BACKENDS AudioSourcePublishingTarget final : public IDomainOperationTarget,
                                                                public IDomainOperationTargetStaging {
public:
    /** @brief Create an authoring target bound to a non-owning runtime sink. */
    AudioSourcePublishingTarget(std::string id, IAudioSourceRuntimeSink* sink);

    /** @brief Target id. */
    TargetId targetId() const override { return TargetId(document_.targetId()); }
    /** @brief Revision. */
    std::uint64_t revision() const override { return document_.revision(); }
    /** @brief Dirty region. */
    EditRegion dirtyRegion() const override { return document_.dirtyRegion(); }
    /** @brief Clears dirty region. */
    void clearDirtyRegion() override { document_.clearDirtyRegion(); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /**
     * @brief Forward authoring capabilities from the owned document.
     * @return Borrowed pointer owned by this target, or null.
     * @lifetime Valid until this target is destroyed or its document is replaced.
     */
    void* queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
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
class EVENGINE_API_BACKENDS AudioMixerTarget final : public ::eve::editing::EditableTargetState,
                                                     public virtual IEditableTarget,
                                                     public IDomainOperationTarget,
                                                     public IDomainOperationTargetStaging {
public:
    /** @brief Audio mixer target. */
    explicit AudioMixerTarget(std::string id);
    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
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
    /** @brief Bus value. */
    static EditorValue busValue(const AudioBusSnapshot& bus);
    /** @brief Parse bus. */
    static EditorResult<AudioBusSnapshot> parseBus(const EditorValue& value);
    /** @brief Would cycle. */
    bool wouldCycle(const ObjectId& id, const ObjectId& parent) const;

    std::string                          id_;
    std::map<ObjectId, AudioBusSnapshot> buses_;
};

}  // namespace eve::audio_editing

namespace eve::audio {
class Source;
}

namespace eve::audio_editing {

/** @brief Optional bridge applying authoring settings to an existing live Source. */
class EVENGINE_API_BACKENDS AudioSourceRuntimeApplier {
public:
    /** @brief Applies . */
    EditorResult<void> apply(const AudioSourceTarget& target, audio::Source* source) const;
};

/** @brief Real Source backend for AudioSourcePublishingTarget. */
class AudioSourceRuntimeSink final : public IAudioSourceRuntimeSink {
public:
    /** @brief Bind a borrowed live Source that must outlive this sink. */
    explicit AudioSourceRuntimeSink(audio::Source* source) : source_(source) {}
    /** @brief Publish. */
    EditorResult<void> publish(const AudioSourceTarget& candidate) override;

private:
    audio::Source* source_ = nullptr;
};

}  // namespace eve::audio_editing
