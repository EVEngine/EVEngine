#pragma once
#include "common/Export.h"


#include "editing/EditableTarget.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"

#include <map>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Project-setting descriptor with deployment and secrecy policy. */
struct ProjectSettingDescriptor {
    PropertyDescriptor property;
    std::string        section;
    bool               requiresRestart = false;
    bool               sensitive       = false;
};

/** @brief Versioned settings schema shared by project and importer documents. */
struct ProjectSettingsSchema {
    std::string                           typeId;
    std::uint32_t                         version = 1;
    std::vector<ProjectSettingDescriptor> settings;
};

/** @brief Schema-driven, reversible settings target with secret-reference enforcement. */
class EVENGINE_API_ORCHESTRATION ProjectSettingsTarget final : public ::eve::editing::EditableTargetState,
                                                               public virtual IEditableTarget,
                                                               public IDomainOperationTarget,
                                                               public IPropertyProvider {
public:
    ProjectSettingsTarget(std::string id, ProjectSettingsSchema schema);
    TargetId         targetId() const override { return TargetId(id_); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime
     * Valid until this target is destroyed or mutated. */
    void*                         queryCapability(const CapabilityId& capability) override;
    EditorResult<void>            applyDomainOperation(const DomainOperation& operation) override;
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;
    /** @brief Return settings whose changed values require subsystem restart. */
    std::vector<PropertyPath> pendingRestart() const;
    /** @brief Capture values without ever serializing raw sensitive values. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load matching-schema values. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    /** @brief Find a setting descriptor. @return Borrowed pointer into the immutable descriptor table, or null.
     * @lifetime Valid for the lifetime of this target. */
    const ProjectSettingDescriptor*    descriptor(const PropertyPath& path) const;
    bool                               selectionMatches(const SelectionSnapshot& selection) const;
    std::string                        id_;
    ProjectSettingsSchema              schema_;
    std::map<std::string, EditorValue> values_;
    std::map<std::string, bool>        restartDirty_;
};

/** @brief Common engine project settings covering content, network and database boundaries. */
EVENGINE_API_ORCHESTRATION ProjectSettingsSchema defaultProjectSettingsSchema();

/** @brief Create per-importer settings without hard-coding a presenter. */
ProjectSettingsSchema importerSettingsSchema(std::string importerId, std::vector<ProjectSettingDescriptor> settings,
                                             std::uint32_t version = 1);

}  // namespace eve::editor
