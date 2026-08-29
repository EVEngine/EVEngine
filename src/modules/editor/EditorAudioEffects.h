#pragma once

#include "editor/EditorAudioTarget.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Stable effect instance with normalized dry/wet mix and typed parameters. */
struct AudioEffectRecord {
    StableId id;
    std::string type;
    bool bypass = false;
    double mix = 1.0;
    EditorValue parameters = EditorValue::Object{};
};

/** @brief Revisioned reversible serial effect chain independent of an audio backend. */
class AudioEffectChainTarget final : public IEditableTargetV2,
                                     public IDomainOperationTarget,
                                     public IDomainOperationTargetStaging {
public:
    explicit AudioEffectChainTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Enumerate effect records in processing order. */
    std::vector<AudioEffectRecord> effects() const;
    /** @brief Plan effect creation or replacement. */
    EditorResult<DomainOperation> makeSet(const AudioEffectRecord& effect) const;
    /** @brief Plan effect removal. */
    EditorResult<DomainOperation> makeDelete(const StableId& id) const;
    /** @brief Plan complete processing-order replacement. */
    EditorResult<DomainOperation> makeReorder(const std::vector<StableId>& order) const;
    /** @brief Report invalid parameter ranges and unsafe chain budgets. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture deterministic schema-version-one effect chain. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a validated effect chain. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Plan assigning this chain snapshot to an existing mixer bus. */
    EditorResult<DomainOperation> makeAssignToBus(const AudioMixerTarget& mixer,
                                                   const ObjectId& bus) const;
private:
    std::string id_; Revision revision_ = 1; EditRegion dirty_;
    std::map<StableId, AudioEffectRecord> effects_;
    std::vector<StableId> order_;
};

/** @brief Runtime backend boundary for atomic effect-chain publication. */
class IAudioEffectChainSink {
public:
    virtual ~IAudioEffectChainSink() = default;
    virtual EditorResult<void> publish(const std::string& chain, Revision revision,
                                       const std::vector<AudioEffectRecord>& effects) = 0;
};

/** @brief Rejects invalid/stale chains before runtime publication. */
class AudioEffectChainPublisher {
public:
    EditorResult<void> publish(const AudioEffectChainTarget& chain,
                               Revision expectedRevision,
                               IAudioEffectChainSink& sink) const;
};

}  // namespace eve::editor
