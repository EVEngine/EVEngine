#pragma once
#include "common/Export.h"


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
class EVENGINE_API_ORCHESTRATION ProcgenScriptDocumentTarget final : public virtual editing::IEditableTarget,
                                                                     public editing::IDomainOperationTarget,
                                                                     public editing::IDomainOperationTargetStaging,
                                                                     public editing::IPropertyProvider {
public:
    /** @brief Construct an empty generator document. @param id Stable target identity. */
    explicit ProcgenScriptDocumentTarget(std::string id);

    /** @brief Capability identity published by describe() for Inspector editing. */
    static editing::CapabilityId propertyCapabilityId() {
        /** @brief Capability id. */
        return editing::CapabilityId("eve.editor.target.procgen-script-properties");
    }

    /** @brief Target id. */
    editing::TargetId targetId() const override { return editing::TargetId(id_); }
    /** @brief Revision. */
    std::uint64_t     revision() const override { return revision_; }
    /** @brief Dirty region. */
    editing::EditRegion dirtyRegion() const override { return dirty_; }
    /** @brief Clears dirty region. */
    void              clearDirtyRegion() override { dirty_.clear(); }
    /** @brief Describe. */
    editing::TargetDescriptor describe() const override;
    /**
     * @brief Query Inspector property capability.
     * @return Borrowed pointer owned by this target, or null.
     * @lifetime Valid until this target is destroyed or replaced by commitDomainState.
     */
    void* queryCapability(const editing::CapabilityId&) override;
    /** @brief Applies domain operation. */
    EditorResult<void> applyDomainOperation(const editing::DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    EditorResult<void> commitDomainState(std::unique_ptr<editing::IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision> currentRevision(const editing::SelectionSnapshot&) const override;
    /** @brief Schema. */
    editing::PropertySchema schema(const editing::SelectionSnapshot&) const override;
    /** @brief Reads . */
    editing::PropertyReadResult read(const editing::SelectionSnapshot&,
                                     const editing::PropertyPath&) const override;
    /** @brief Make set. */
    EditorResult<editing::DomainOperation> makeSet(const editing::SelectionSnapshot&,
                                                   const editing::PropertyPath&, const EditorValue&,
                                                   editing::PropertySetMode) const override;
    /** @brief Make reset. */
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

    /** @brief Uri. */
    const std::string&              uri() const { return uri_; }
    /** @brief Module id. */
    const std::string&              moduleId() const { return moduleId_; }
    /** @brief Display name. */
    const std::string&              displayName() const { return displayName_; }
    /** @brief Kind. */
    const std::string&              kind() const { return kind_; }
    /** @brief Params. */
    const std::vector<procgen::ParamDescriptor>& params() const { return params_; }
    /** @brief Values. */
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

    /** @brief Validate. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Snapshot value. */
    EditorValue                   snapshotValue() const;
    /** @brief Loads snapshot. */
    EditorResult<void>            loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool matches(const editing::SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue contentValue() const;
    /** @brief Schema value. */
    EditorValue schemaValue() const;
    /** @brief Default value. */
    static EditorValue defaultValue(const procgen::ParamDescriptor& param);
    /** @brief Property type. */
    static editing::PropertyType propertyType(procgen::ParamKind kind);
    /** @brief Applies spec. */
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
