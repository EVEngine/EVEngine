#pragma once

#include <string>
#include <vector>
#include "common/Export.h"
#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
namespace eve::sceneloader {
class SceneLoader;
}
namespace eve::sceneloader_editing {
using namespace eve::editing;
using EditorValue      = eve::editing::Value;
using EditorStatus     = eve::editing::Status;
using EditorDiagnostic = eve::editing::Diagnostic;
using editing::Result;
/** @brief Renderer-neutral persisted scene importer settings. */
struct SceneImportValue {
    std::string sourceAsset;
    std::string preset      = "balanced";
    bool        triangulate = true, generateNormals = true, joinVertices = true, flipUvs = true, improveCache = true;
    bool sharedMeshes = true, mipmaps = true, importLights = true, importCameras = false, importAnimations = true;
};
/** @brief EVENGINE_API_ORCHESTRATION public API. */
class EVENGINE_API_ORCHESTRATION SceneImportTarget final : public ::eve::editing::EditableTargetState,
                                                           public virtual IEditableTarget,
                                                           public IDomainOperationTarget,
                                                           public IDomainOperationTargetStaging,
                                                           public IPropertyProvider {
public:
    /** @brief Scene import target. */
    explicit SceneImportTarget(std::string id);
    /** @brief Target id. */
    TargetId                                targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor                        describe() const override;
    /**
     * @brief Query an optional stable editing capability.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    void*                                   queryCapability(const CapabilityId&) override;
    /** @brief Applies domain operation. */
    Result<void>                      applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    Result<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    Result<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    /** @brief Make reset. */
    Result<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Value. */
    const SceneImportValue&                 value() const { return value_; }
    /** @brief Validate. */
    std::vector<EditorDiagnostic>           validate() const;
    /** @brief Snapshot value. */
    EditorValue                             snapshotValue() const;
    /** @brief Loads snapshot. */
    Result<void>                      loadSnapshot(const EditorValue&);

private:
    /** @brief Matches. */
    bool              matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue       contentValue() const;
    std::string       id_;
    SceneImportValue  value_;
};
/** @brief SceneImportPreflight public API. */
struct SceneImportPreflight {
    editing::Revision        sourceRevision = 0;
    int                      nodes = 0, meshNodes = 0, added = 0, removed = 0, modified = 0, moved = 0;
    std::vector<std::string> warnings, sockets, collisions;
};
/** @brief EVENGINE_API_ORCHESTRATION public API. */
class EVENGINE_API_ORCHESTRATION SceneImportPreflightRuntime {
public:
    /** @brief Inspect. */
    Result<SceneImportPreflight> inspect(const SceneImportTarget&, sceneloader::SceneLoader*) const;
};
}  // namespace eve::sceneloader_editing
