#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editing/EditableTarget.h"
#include "virtualgeometry/Builder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::virtualgeometry { struct VirtualGeometryAsset; }

namespace eve::editor {

/** @brief Authored virtual-geometry import and LOD-preview settings. */
struct VirtualGeometryImportValue {
    std::string sourceAsset;
    virtualgeometry::VirtualGeometryBuilder::Options builder;
    float previewFov = 60.f;
    int previewHeight = 1080;
    float errorPixels = 1.f;
    float nearDistance = 1.f;
    float farDistance = 1000.f;
    int distanceSamples = 32;
};

/** @brief Revisioned VirtualGeometry importer preset. */
class VirtualGeometryDocumentTarget final : public virtual IEditableTarget,
                                            public IDomainOperationTarget,
                                            public IDomainOperationTargetStaging,
                                            public IPropertyProvider {
public:
    explicit VirtualGeometryDocumentTarget(std::string id);
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
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection,const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection,const PropertyPath& path,
                                          const EditorValue& value,PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    const VirtualGeometryImportValue& value() const { return value_; }
    /** @brief Validate builder invariants, CPU budgets and preview sweep. */
    std::vector<EditorDiagnostic> validate() const;
    EditorValue snapshotValue() const;
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
private:
    bool matches(const SelectionSnapshot& selection) const;
    EditorValue contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue content,std::string property) const;
    std::string id_;Revision revision_=1;EditRegion dirty_;VirtualGeometryImportValue value_;
};

/** @brief Owned neutral triangle mesh supplied by an importer/AssetDB adapter. */
struct VirtualGeometryMeshData { std::vector<float> positions,normals;std::vector<std::uint32_t> indices; };

/** @brief Asset resolver boundary for VirtualGeometry preprocessing. */
class IVirtualGeometryMeshResolver {
public:
    virtual ~IVirtualGeometryMeshResolver()=default;
    /** @brief Return an owned triangulated mesh for one asset generation. */
    virtual EditorResult<VirtualGeometryMeshData> resolve(const std::string& assetId) const=0;
};

/** @brief One sample in the logarithmic camera-distance LOD cost curve. */
struct VirtualGeometryLodSample { float distance=0;int clusters=0;std::uint64_t triangles=0;std::vector<int> levelHistogram; };

/** @brief Immutable quality/cost report for a built virtual-geometry generation. */
struct VirtualGeometryBuildArtifact {
    Revision sourceRevision=0;int vertices=0;std::uint64_t sourceTriangles=0,storedTriangles=0;
    int clusters=0,maxLod=0,roots=0;std::uint64_t checksum=0;
    std::vector<VirtualGeometryLodSample> lodCurve;
};

/** @brief Candidate-first CPU cluster-DAG builder and LOD preview service. */
class VirtualGeometryBuildRuntime {
public:
    VirtualGeometryBuildRuntime();~VirtualGeometryBuildRuntime();
    /** @brief Resolve, validate and build fully before replacing the active asset. */
    EditorResult<VirtualGeometryBuildArtifact> build(const VirtualGeometryDocumentTarget& document,
                                                       const IVirtualGeometryMeshResolver& resolver);
    /** @brief Access the published asset. @return Borrowed pointer owned by this runtime, or null. @lifetime Valid until the next build or runtime destruction. */
    const virtualgeometry::VirtualGeometryAsset* asset() const { return asset_.get(); }
    Revision revision() const { return revision_; }
private:
    std::unique_ptr<virtualgeometry::VirtualGeometryAsset> asset_;Revision revision_=0;
};

} // namespace eve::editor
