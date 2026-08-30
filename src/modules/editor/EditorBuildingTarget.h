#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTarget.h"

#include <map>
#include <string>
#include <vector>

namespace eve::building {
class PlacementWorld;
}

namespace eve::editor {

/** @brief Persistent garrison member copied through building editor undo. */
struct BuildingGarrisonMemberRecord {
    std::string id;
    std::string type;
    std::vector<std::string> tags;
};

/** @brief Complete runtime-neutral placed-building snapshot. */
struct BuildingInstanceSnapshot {
    int instanceId = 0;
    std::string buildingId;
    int cellX = 0;
    int cellY = 0;
    double worldX = 0.0;
    double worldY = 0.0;
    double elevation = 0.0;
    double rotationDegrees = 0.0;
    std::string channel;
    std::map<std::string, std::string> properties;
    std::vector<std::string> tags;
    std::vector<BuildingGarrisonMemberRecord> garrison;
    std::uint64_t garrisonRevision = 0;
};

/** @brief One occupied or rejected footprint cell in a placement preview. */
struct BuildingFootprintCell {
    int x = 0;
    int y = 0;
    bool inBounds = false;
    int occupant = 0;
    int terrain = 0;
};

/** @brief Structured footprint/snap preview tied to a world revision. */
struct BuildingPlacementPreview {
    EditorStatus status = EditorStatus::Failed;
    Revision worldRevision = 0;
    int snappedCellX = 0;
    int snappedCellY = 0;
    double snappedWorldX = 0.0;
    double snappedWorldY = 0.0;
    double elevation = 0.0;
    double normalizedRotation = 0.0;
    std::vector<BuildingFootprintCell> cells;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Live PlacementWorld target with reversible place/move/remove operations. */
class BuildingPlacementTarget final : public virtual IEditableTarget, public IDomainOperationTarget {
public:
    /** @brief Stable capability id for placement preview and mutation. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.building-placement"); }
    /** @brief Bind a borrowed world that must outlive the target. */
    BuildingPlacementTarget(std::string id, building::PlacementWorld* world);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;

    /** @brief Return a complete immutable instance snapshot. */
    EditorResult<BuildingInstanceSnapshot> instance(int instanceId) const;
    /** @brief Return the smallest currently unused positive instance id. */
    int nextAvailableInstanceId() const;
    /** @brief Preview snap, normalized rotation, footprint cells and validation reasons. */
    BuildingPlacementPreview preview(const std::string& buildingId, double worldX, double worldY,
                                     double elevation, double rotationDegrees,
                                     int excludeInstanceId = 0) const;
    /** @brief Plan reversible exact-id placement after full footprint validation. */
    EditorResult<DomainOperation> makePlace(const BuildingInstanceSnapshot& placed) const;
    /** @brief Plan reversible movement of an existing building. */
    EditorResult<DomainOperation> makeMove(int instanceId, int cellX, int cellY,
                                           double rotationDegrees) const;
    /** @brief Plan reversible removal preserving properties, tags and garrison state. */
    EditorResult<DomainOperation> makeRemove(int instanceId) const;
    /** @brief Capture all instances in deterministic insertion order. */
    EditorValue snapshotValue() const;

private:
    std::string id_;
    building::PlacementWorld* world_ = nullptr;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
};

}  // namespace eve::editor
