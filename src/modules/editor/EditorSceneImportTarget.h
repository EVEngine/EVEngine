#pragma once
#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"
#include <string>
#include <vector>
namespace eve::sceneloader { class SceneLoader; }
namespace eve::editor {
/** @brief Renderer-neutral persisted scene importer settings. */
struct SceneImportValue {
    std::string sourceAsset;
    std::string preset="balanced";
    bool triangulate=true,generateNormals=true,joinVertices=true,flipUvs=true,improveCache=true;
    bool sharedMeshes=true,mipmaps=true,importLights=true,importCameras=false,importAnimations=true;
};
class SceneImportTarget final:public IEditableTargetV2,public IDomainOperationTarget,
 public IDomainOperationTargetStaging,public IPropertyProvider{
public:explicit SceneImportTarget(std::string id);const std::string&targetId()const override{return id_;}unsigned long long revision()const override{return revision_;}EditRegion dirtyRegion()const override{return dirty_;}void clearDirtyRegion()override{dirty_.clear();}TargetDescriptor describe()const override;void*queryCapability(const CapabilityId&)override;EditorResult<void>applyDomainOperation(const DomainOperation&)override;std::unique_ptr<IDomainOperationTarget>cloneDomainState()const override;EditorResult<void>commitDomainState(std::unique_ptr<IDomainOperationTarget>)override;eve::Result<eve::Revision>currentRevision(const SelectionSnapshot&)const override;PropertySchema schema(const SelectionSnapshot&)const override;PropertyReadResult read(const SelectionSnapshot&,const PropertyPath&)const override;EditorResult<DomainOperation>makeSet(const SelectionSnapshot&,const PropertyPath&,const EditorValue&,PropertySetMode)const override;EditorResult<DomainOperation>makeReset(const SelectionSnapshot&,const PropertyPath&)const override;const SceneImportValue&value()const{return value_;}std::vector<EditorDiagnostic>validate()const;EditorValue snapshotValue()const;EditorResult<void>loadSnapshot(const EditorValue&);
private:bool matches(const SelectionSnapshot&)const;EditorValue contentValue()const;std::string id_;Revision revision_=1;EditRegion dirty_;SceneImportValue value_;};
struct SceneImportPreflight{Revision sourceRevision=0;int nodes=0,meshNodes=0,added=0,removed=0,modified=0,moved=0;std::vector<std::string>warnings,sockets,collisions;};
class SceneImportPreflightRuntime{public:EditorResult<SceneImportPreflight>inspect(const SceneImportTarget&,sceneloader::SceneLoader*)const;};
} // namespace eve::editor
