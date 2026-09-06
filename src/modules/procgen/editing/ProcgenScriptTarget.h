#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "procgen/ParamSchema.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen_editing {

using EditorDiagnostic = editing::Diagnostic;
using EditorStatus     = editing::Status;
using EditorValue      = editing::Value;
template <class T>
using EditorResult = editing::Result<T>;

/**
 * @brief Authored identity and reflected parameter schema for one script generator.
 *
 * @ownership Copied into the document. Schema descriptors do not retain script objects.
 */
struct ProcgenScriptModuleSpec {
    std::string                      uri;
    std::string                      id;
    std::string                      displayName;
    std::string                      kind = "points";
    std::vector<procgen::ParamDescriptor> params;
};

/**
 * @brief Revisioned Params document for a script-hosted procedural generator.
 *
 * The document is the unique owner of parameter values. Preview PointSets are
 * not stored here; the editor copies them after a successful script rebuild.
 *
 * @ownership Document owns schema and values. Snapshots are copied trees.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class ProcgenScriptDocumentTarget final : public virtual editing::IEditableTarget,
                                          public editing::IDomainOperationTarget,
                                          public editing::IDomainOperationTargetStaging,
                                          public editing::IPropertyProvider {
public:
    /** @brief Construct an empty generator document. @param id Stable target identity. */
    explicit ProcgenScriptDocumentTarget(std::string id);

    /** @brief Capability identity published by describe() for Inspector editing. */
    static editing::CapabilityId propertyCapabilityId() {
        return editing::CapabilityId("eve.editor.target.procgen-script-properties");
    }

    editing::TargetId targetId() const override { return editing::TargetId(id_); }
    std::uint64_t     revision() const override { return revision_; }
    editing::EditRegion dirtyRegion() const override { return dirty_; }
    void              clearDirtyRegion() override { dirty_.clear(); }
    editing::TargetDescriptor describe() const override;
    /**
     * @brief Query Inspector property capability.
     * @return Borrowed pointer owned by this target, or null.
     * @lifetime Valid until this target is destroyed or replaced by commitDomainState.
     */
    void* queryCapability(const editing::CapabilityId&) override;
    EditorResult<void> applyDomainOperation(const editing::DomainOperation&) override;
    std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<editing::IDomainOperationTarget>) override;
    eve::Result<eve::Revision> currentRevision(const editing::SelectionSnapshot&) const override;
    editing::PropertySchema schema(const editing::SelectionSnapshot&) const override;
    editing::PropertyReadResult read(const editing::SelectionSnapshot&,
                                     const editing::PropertyPath&) const override;
    EditorResult<editing::DomainOperation> makeSet(const editing::SelectionSnapshot&,
                                                   const editing::PropertyPath&, const EditorValue&,
                                                   editing::PropertySetMode) const override;
    EditorResult<editing::DomainOperation> makeReset(const editing::SelectionSnapshot&,
                                                     const editing::PropertyPath&) const override;

    /**
     * @brief Parse a schema array into a module spec.
     * @param schema Array of parameter objects with key/kind/default metadata.
     */
    static EditorResult<ProcgenScriptModuleSpec> parseSpec(std::string uri, std::string id, std::string displayName,
                                                           std::string kind, const EditorValue& schema);

    /** @brief Replace module identity and schema, remapping values (drop unknown, fill defaults). */
    EditorResult<editing::DomainOperation> makeLoadModule(ProcgenScriptModuleSpec spec) const;

    const std::string&              uri() const { return uri_; }
    const std::string&              moduleId() const { return moduleId_; }
    const std::string&              displayName() const { return displayName_; }
    const std::string&              kind() const { return kind_; }
    const std::vector<procgen::ParamDescriptor>& params() const { return params_; }
    const EditorValue::Object&      values() const { return values_; }
    /**
     * @brief Find one reflected parameter by stable key.
     * @param key Parameter key from the loaded schema.
     * @return Borrowed schema descriptor owned by this document, or null when absent.
     * @ownership Borrowed; callers must not delete the pointer.
     * @lifetime Valid until this document is destroyed or loadModule/applyDomainOperation mutates schema.
     * @nullable Yes when the key is unknown.
     */
    const procgen::ParamDescriptor* findParam(const std::string& key) const;

    std::vector<EditorDiagnostic> validate() const;
    EditorValue                   snapshotValue() const;
    EditorResult<void>            loadSnapshot(const EditorValue&);

private:
    bool matches(const editing::SelectionSnapshot&) const;
    EditorValue contentValue() const;
    EditorValue schemaValue() const;
    static EditorValue defaultValue(const procgen::ParamDescriptor& param);
    static editing::PropertyType propertyType(procgen::ParamKind kind);
    void applySpec(ProcgenScriptModuleSpec spec);

    std::string                         id_;
    std::string                         uri_;
    std::string                         moduleId_;
    std::string                         displayName_;
    std::string                         kind_ = "points";
    std::vector<procgen::ParamDescriptor> params_;
    EditorValue::Object                 values_;
    editing::Revision                   revision_ = 1;
    editing::EditRegion                 dirty_;
};

}  // namespace eve::procgen_editing
