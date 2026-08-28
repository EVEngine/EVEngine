#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTargetV2.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Stable map layer descriptor used by outliner presenters. */
struct MapLayerRecord {
    StableId id;
    std::string name;
    std::string kind = "tile";
    bool visible = true;
    bool locked = false;
    double opacity = 1.0;
    int order = 0;
};

/** @brief Stable editable control point in a road or path spline. */
struct MapSplinePointRecord {
    StableId id;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double width = 4.0;
};

/** @brief Road spline authoring record independent from rendering/mesh generation. */
struct MapRoadRecord {
    StableId id;
    StableId layer;
    std::string name;
    std::string materialAsset;
    bool closed = false;
    std::vector<MapSplinePointRecord> points;
};

/** @brief Stable object placement stored on an object layer. */
struct MapPlacementRecord {
    StableId id;
    StableId layer;
    std::string asset;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rotationX = 0.0;
    double rotationY = 0.0;
    double rotationZ = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double scaleZ = 1.0;
};

/** @brief Deterministic renderer-neutral road strip preview. */
struct MapRoadPreviewResult {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    StableId road;
    double centerlineLength = 0.0;
    EditorValue mesh;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Runtime-neutral sink receiving revision-tagged generated road meshes. */
class IMapRoadMeshSink {
public:
    virtual ~IMapRoadMeshSink() = default;
    /** @brief Atomically publish or replace one generated road mesh. */
    virtual EditorResult<void> publishRoad(const std::string& document, const StableId& road,
                                           Revision revision, const EditorValue& mesh) = 0;
    /** @brief Remove a previously published road mesh. */
    virtual EditorResult<void> removeRoad(const std::string& document, const StableId& road) = 0;
};

/** @brief Imported object record before stable placement ids are assigned. */
struct MapObjectImportRecord {
    std::string sourceId;
    std::string asset;
    double x = 0.0, y = 0.0, z = 0.0;
    double rotationX = 0.0, rotationY = 0.0, rotationZ = 0.0;
    double scaleX = 1.0, scaleY = 1.0, scaleZ = 1.0;
};

/** @brief Side-effect-free batch import plan for map object placements. */
struct MapObjectImportPlan {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    std::vector<DomainOperation> operations;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Layer, spline and placement editing capability for map presenters. */
class IMapStructureEditTarget {
public:
    virtual ~IMapStructureEditTarget() = default;
    /** @brief Stable capability id for map structure operations. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.map-structure"); }
    /** @brief Enumerate layers in visual order. */
    virtual std::vector<MapLayerRecord> mapLayers() const = 0;
    /** @brief Enumerate roads in stable id order. */
    virtual std::vector<MapRoadRecord> mapRoads() const = 0;
    /** @brief Enumerate placements in stable id order. */
    virtual std::vector<MapPlacementRecord> mapPlacements() const = 0;
    /** @brief Plan reversible layer creation. */
    virtual EditorResult<DomainOperation> makeCreateLayer(const MapLayerRecord& layer) const = 0;
    /** @brief Plan deletion of an empty layer. */
    virtual EditorResult<DomainOperation> makeDeleteLayer(const StableId& layer) const = 0;
    /** @brief Plan reversible layer metadata replacement. */
    virtual EditorResult<DomainOperation> makeSetLayer(const MapLayerRecord& layer) const = 0;
    /** @brief Plan reversible road creation or replacement. */
    virtual EditorResult<DomainOperation> makeSetRoad(const MapRoadRecord& road) const = 0;
    /** @brief Plan reversible road deletion. */
    virtual EditorResult<DomainOperation> makeDeleteRoad(const StableId& road) const = 0;
    /** @brief Plan reversible placement creation or replacement. */
    virtual EditorResult<DomainOperation> makeSetPlacement(const MapPlacementRecord& placement) const = 0;
    /** @brief Plan reversible placement deletion. */
    virtual EditorResult<DomainOperation> makeDeletePlacement(const StableId& placement) const = 0;
};

/** @brief UI-neutral map structure document with reversible domain operations. */
class MapDocumentTarget final : public IEditableTargetV2,
                                public IDomainOperationTarget,
                                public IDomainOperationTargetStaging,
                                public IMapStructureEditTarget {
public:
    explicit MapDocumentTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;

    std::vector<MapLayerRecord> mapLayers() const override;
    std::vector<MapRoadRecord> mapRoads() const override;
    std::vector<MapPlacementRecord> mapPlacements() const override;
    EditorResult<DomainOperation> makeCreateLayer(const MapLayerRecord& layer) const override;
    EditorResult<DomainOperation> makeDeleteLayer(const StableId& layer) const override;
    EditorResult<DomainOperation> makeSetLayer(const MapLayerRecord& layer) const override;
    EditorResult<DomainOperation> makeSetRoad(const MapRoadRecord& road) const override;
    EditorResult<DomainOperation> makeDeleteRoad(const StableId& road) const override;
    EditorResult<DomainOperation> makeSetPlacement(const MapPlacementRecord& placement) const override;
    EditorResult<DomainOperation> makeDeletePlacement(const StableId& placement) const override;

    /** @brief Validate layers, spline geometry and placement references. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Build a deterministic XZ road strip for viewport preview and budget diagnostics. */
    MapRoadPreviewResult previewRoad(const StableId& road, int triangleBudget = 10000) const;
    /** @brief Capture deterministic map structure data. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load schema-version-one map structure data. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<StableId, MapLayerRecord> layers_;
    std::map<StableId, MapRoadRecord> roads_;
    std::map<StableId, MapPlacementRecord> placements_;
};

/** @brief Publishes validated road previews while rejecting stale document revisions. */
class MapRoadMeshPublisher {
public:
    /** @brief Generate and publish one road mesh to the supplied host sink. */
    EditorResult<void> publish(const MapDocumentTarget& document, const StableId& road,
                               Revision expectedRevision, IMapRoadMeshSink& sink,
                               int triangleBudget = 10000) const;
};

/** @brief Converts external object records into one atomic placement transaction plan. */
class MapObjectImporter {
public:
    /** @brief Plan stable-id placement creation without mutating the map document. */
    MapObjectImportPlan plan(const MapDocumentTarget& document, const StableId& layer,
                             const std::vector<MapObjectImportRecord>& records,
                             const std::string& idPrefix) const;
};

}  // namespace eve::editor
