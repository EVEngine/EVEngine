#pragma once
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "animation/editing/ControlRigDocument.h"
#include "animation/editor/ControlRigEditor.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorTransactionService.h"

namespace eve::animation_editor {
class RigCommandTarget {
public:
    virtual ~RigCommandTarget()                                                                         = default;
    [[nodiscard]] virtual Result<editing::DomainOperation> prepare(const editing::Value& payload) const = 0;
};
struct ControlRigEditor::Impl {
    struct LiveTarget final : editing::IDomainOperationTarget,
                              editing::IDomainOperationTargetStaging,
                              editing::IEditingSnapshotProvider,
                              RigCommandTarget {
        Impl& owner;
        explicit LiveTarget(Impl& value) : owner(value) {}
        editing::TargetId          targetId() const override { return owner.document.targetId(); }
        std::uint64_t              revision() const override { return owner.document.revision(); }
        editing::EditRegion        dirtyRegion() const override { return owner.document.dirtyRegion(); }
        void                       clearDirtyRegion() override { owner.document.clearDirtyRegion(); }
        editing::TargetDescriptor  describe() const override { return owner.document.describe(); }
        editing::Value             snapshotValue() const override;
        [[nodiscard]] Result<void> applyDomainOperation(const editing::DomainOperation& operation) override;
        std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override {
            return owner.document.cloneDomainState();
        }
        [[nodiscard]] Result<void> commitDomainState(
            std::unique_ptr<editing::IDomainOperationTarget> candidate) override;
        [[nodiscard]] Result<editing::DomainOperation> prepare(const editing::Value& payload) const override;
    };
    animation_editing::ControlRigDocument    document;
    LiveTarget                               target;
    editor::LocalWorldAuthority              authority;
    editor::LocalTransactionBackend          transactions;
    std::unique_ptr<animation::AnimSkeleton> skeleton;
    std::unique_ptr<animation::ControlRig>   rig;
    animation::AnimPose                      input, pose;
    double                                   playhead = 0;
    std::uint64_t                            sequence = 0;
    editor::EditorTargetCoordinator*         host     = nullptr;
    std::weak_ptr<void>                      hostLifetime;
    explicit Impl(std::string id)
        : document(std::move(id)), target(*this), authority(&target), transactions(&authority) {}
    ~Impl();
    [[nodiscard]] Result<void> rebuild();
    [[nodiscard]] Result<void> validate(const animation::RigDefinition& definition) const;
};
}  // namespace eve::animation_editor
