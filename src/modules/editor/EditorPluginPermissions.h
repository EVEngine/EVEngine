#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTargetV2.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Explicit project-owned permission decision for one plugin capability scope. */
struct PluginPermissionGrant {
    StableId id;
    std::string plugin;
    std::string capability;
    std::string scope;
    std::string decision = "ask";
};

/** @brief Revisioned plugin permission policy with reversible, auditable grants. */
class PluginPermissionTarget final : public IEditableTargetV2,
                                     public IDomainOperationTarget,
                                     public IDomainOperationTargetStaging {
public:
    explicit PluginPermissionTarget(std::string id);
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
    /** @brief Enumerate grants in stable-id order. */
    std::vector<PluginPermissionGrant> grants() const;
    /** @brief Plan creation or replacement after least-privilege validation. */
    EditorResult<DomainOperation> makeSet(const PluginPermissionGrant& grant) const;
    /** @brief Plan grant removal. */
    EditorResult<DomainOperation> makeRemove(const StableId& id) const;
    /** @brief Resolve exact plugin/capability/scope policy, defaulting to ask. */
    std::string decision(const std::string& plugin, const std::string& capability,
                         const std::string& scope) const;
    /** @brief Capture deterministic permission policy. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a validated permission policy. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
private:
    std::string id_; Revision revision_ = 1; EditRegion dirty_;
    std::map<StableId, PluginPermissionGrant> grants_;
};

}  // namespace eve::editor
