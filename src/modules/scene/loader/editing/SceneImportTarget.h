#pragma once
#include <string>
#include <vector>
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
template <class T>
using EditorResult = eve::editing::Result<T>;
/** @brief Renderer-neutral persisted scene importer settings. */
struct SceneImportValue {
    std::string sourceAsset;
    std::string preset      = "balanced";
    bool        triangulate = true, generateNormals = true, joinVertices = true, flipUvs = true, improveCache = true;
    bool sharedMeshes = true, mipmaps = true, importLights = true, importCameras = false, importAnimations = true;
};
class SceneImportTarget final : public virtual IEditableTarget,
                                public IDomainOperationTarget,
                                public IDomainOperationTargetStaging,
                                public IPropertyProvider {
public:
    explicit SceneImportTarget(std::string id);
    TargetId                                targetId() const override { return TargetId(id_); }
    std::uint64_t                           revision() const override { return revision_; }
    EditRegion                              dirtyRegion() const override { return dirty_; }
    void                                    clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor                        describe() const override;
    /**
     * @brief Query an optional stable editing capability.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or the capability is explicitly invalidated.
     */
    void*                                   queryCapability(const CapabilityId&) override;
    EditorResult<void>                      applyDomainOperation(const DomainOperation&) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    EditorResult<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    EditorResult<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;
    const SceneImportValue&                 value() const { return value_; }
    std::vector<EditorDiagnostic>           validate() const;
    EditorValue                             snapshotValue() const;
    EditorResult<void>                      loadSnapshot(const EditorValue&);

private:
    bool              matches(const SelectionSnapshot&) const;
    EditorValue       contentValue() const;
    std::string       id_;
    editing::Revision revision_ = 1;
    EditRegion        dirty_;
    SceneImportValue  value_;
};
struct SceneImportPreflight {
    editing::Revision        sourceRevision = 0;
    int                      nodes = 0, meshNodes = 0, added = 0, removed = 0, modified = 0, moved = 0;
    std::vector<std::string> warnings, sockets, collisions;
};
class SceneImportPreflightRuntime {
public:
    EditorResult<SceneImportPreflight> inspect(const SceneImportTarget&, sceneloader::SceneLoader*) const;
};
}  // namespace eve::sceneloader_editing
