#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingTargetOperations.h"
#include "procgen/editing/ProcgenEditingTypes.h"
#include "procgen/spline/SplinePath.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::procgen_editing {

using editing::CapabilityId;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::TargetDescriptor;
using editing::TargetId;

/** @brief Stable authoring control point for a 3D spline path. */
struct SplinePathControlPoint {
    StableId     id;
    std::int64_t order = 0;
    double       x = 0.0, y = 0.0, z = 0.0;
    double       inX = 0.0, inY = 0.0, inZ = 0.0;
    double       outX = 0.0, outY = 0.0, outZ = 0.0;
    double       rollDegrees = 0.0;
    double       scaleX = 1.0, scaleY = 1.0;
    bool         breakBefore  = false;
    double       pitchDegrees = 0.0, yawDegrees = 0.0;
};

/** @brief Spline authoring settings stored independently from runtime caches. */
struct SplinePathSettings {
    std::string kind   = "catmullRom";
    bool        closed = false;
};

/** @brief UI-neutral reversible spline editing capability. */
class ISplinePathDocumentEditTarget {
public:
    virtual ~ISplinePathDocumentEditTarget() = default;
    /** @brief Return the stable capability id. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.procgen.target.spline-path-document"); }
    /** @brief Plan creation or atomic replacement of one control point. */
    virtual Result<DomainOperation> makeSetPoint(const SplinePathControlPoint& point) const = 0;
    /** @brief Plan removal of one stable control point. */
    virtual Result<DomainOperation> makeDeletePoint(const StableId& point) const = 0;
    /** @brief Plan interpolation and closure settings replacement. */
    virtual Result<DomainOperation> makeSetSettings(const SplinePathSettings& settings) const = 0;
    /** @brief Plan shape-preserving insertion into one curve segment. */
    virtual Result<DomainOperation> makeInsertPointAt(int segment, double t, const StableId& point) const = 0;
    /** @brief Plan anchor snapping to a positive world-space grid size. */
    virtual Result<DomainOperation> makeSnapPoint(const StableId& point, double gridSize) const = 0;
    /** @brief Plan snapping every anchor to a positive world-space grid in one reversible operation. */
    virtual Result<DomainOperation> makeSnapAll(double gridSize) const = 0;
    /** @brief Plan reversal of traversal direction while preserving Bezier geometry. */
    virtual Result<DomainOperation> makeFlipDirection() const = 0;
    /** @brief Plan splitting or reconnecting the chunk boundary before one point. */
    virtual Result<DomainOperation> makeSetChunkBreak(const StableId& point, bool disconnected) const = 0;
    /** @brief Plan appending one disconnected two-point chunk in one reversible operation. */
    virtual Result<DomainOperation> makeAppendChunk(const SplinePathControlPoint& first,
                                                          const SplinePathControlPoint& second) const = 0;
    /** @brief Plan removing one zero-based disconnected chunk in one reversible operation. */
    virtual Result<DomainOperation> makeDeleteChunk(int chunk) const = 0;
    /** @brief Plan absolute point-local pitch, yaw, and roll replacement. */
    virtual Result<DomainOperation> makeSetPointRotation(const StableId& point, double pitchDegrees,
                                                               double yawDegrees, double rollDegrees) const = 0;
    /** @brief Plan resetting one point-local orientation to identity. */
    virtual Result<DomainOperation> makeResetPointRotation(const StableId& point) const = 0;
    /** @brief Plan moving an interior control point to the midpoint of its connected neighbours. */
    virtual Result<DomainOperation> makeCenterPoint(const StableId& point) const = 0;
    /** @brief Plan mirroring the complete path and handles across x, y, or z. */
    virtual Result<DomainOperation> makeMirrorAxis(std::string_view axis) const = 0;
};

/**
 * @brief Owning spline authoring document with transactional operations and schema-versioned snapshots.
 *
 * Unknown snapshot fields are ignored. Known fields are validated into an isolated candidate before commit.
 */
class EVENGINE_API_ORCHESTRATION SplinePathDocument final : public ::eve::editing::EditableTargetState,
                                                            public virtual IEditableTarget,
                                                            public IDomainOperationTarget,
                                                            public IDomainOperationTargetStaging,
                                                            public ISplinePathDocumentEditTarget {
public:
    /** @brief Construct an empty document with a stable target id. */
    explicit SplinePathDocument(std::string id);
    TargetId         targetId() const override { return TargetId(id_); }
    TargetDescriptor describe() const override;
    /**
     * @brief Query the spline editing capability.
     * @param capability Stable requested capability id.
     * @return Borrowed pointer owned by this document, or null when unsupported.
     * @lifetime Valid until this document is destroyed.
     */
    void*              queryCapability(const CapabilityId& capability) override;
    Result<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    Result<DomainOperation>    makeSetPoint(const SplinePathControlPoint& point) const override;
    Result<DomainOperation>    makeDeletePoint(const StableId& point) const override;
    Result<DomainOperation>    makeSetSettings(const SplinePathSettings& settings) const override;
    Result<DomainOperation>    makeInsertPointAt(int segment, double t, const StableId& point) const override;
    Result<DomainOperation>    makeSnapPoint(const StableId& point, double gridSize) const override;
    Result<DomainOperation>    makeSnapAll(double gridSize) const override;
    Result<DomainOperation>    makeFlipDirection() const override;
    Result<DomainOperation>    makeSetChunkBreak(const StableId& point, bool disconnected) const override;
    Result<DomainOperation>    makeAppendChunk(const SplinePathControlPoint& first,
                                                     const SplinePathControlPoint& second) const override;
    Result<DomainOperation>    makeDeleteChunk(int chunk) const override;
    Result<DomainOperation>    makeSetPointRotation(const StableId& point, double pitchDegrees, double yawDegrees,
                                                          double rollDegrees) const override;
    Result<DomainOperation>    makeResetPointRotation(const StableId& point) const override;
    Result<DomainOperation>    makeCenterPoint(const StableId& point) const override;
    Result<DomainOperation>    makeMirrorAxis(std::string_view axis) const override;

    /** @brief Return points in deterministic order and stable-id tie order. */
    [[nodiscard]] std::vector<SplinePathControlPoint> points() const;
    /** @brief Return current interpolation and closure settings. */
    [[nodiscard]] const SplinePathSettings& settings() const noexcept { return settings_; }
    /** @brief Compile an owning runtime spline without retaining document references. */
    [[nodiscard]] Result<procgen::SplinePath> compilePath() const;
    /** @brief Capture schema `eve.procgen.splinePath` version one. */
    [[nodiscard]] EditorValue snapshotValue() const;
    /** @brief Atomically load a validated version-one snapshot. */
    [[nodiscard]] Result<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string                                id_;
    SplinePathSettings                         settings_;
    std::map<StableId, SplinePathControlPoint> points_;
};

/** @brief Compile a version-one spline snapshot for isolated graph preview. */
[[nodiscard]] Result<procgen::SplinePath> compileSplinePathSnapshot(const EditorValue& snapshot);

}  // namespace eve::procgen_editing
