#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTarget.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Stable authoring record for one crowd agent. */
struct CrowdAgentRecord {
    StableId id;
    std::string archetype;
    double x = 0.0, y = 0.0;
    double heading = 0.0;
    double radius = 0.5;
    double maximumSpeed = 4.0;
    std::string behavior = "idle";
    StableId path;
};

/** @brief Stable polygonal sensing, avoidance or gameplay zone. */
struct CrowdZoneRecord {
    StableId id;
    std::string name;
    std::string kind = "avoid";
    std::vector<std::array<double, 2>> points;
    double weight = 1.0;
    bool enabled = true;
};

/** @brief Stable waypoint used by a crowd path. */
struct CrowdWaypointRecord {
    StableId id;
    double x = 0.0, y = 0.0;
    double arriveRadius = 1.0;
    double waitSeconds = 0.0;
};

/** @brief Stable ordered path shared by multiple agents. */
struct CrowdPathRecord {
    StableId id;
    std::string name;
    bool loop = false;
    std::vector<CrowdWaypointRecord> points;
};

/** @brief Renderer-neutral line, circle and label primitives for AI overlays. */
struct CrowdOverlayPrimitive {
    std::string kind;
    StableId object;
    std::vector<double> values;
    std::string label;
};

/** @brief Revision-tagged deterministic viewport overlay. */
struct CrowdOverlayResult {
    EditorStatus status = EditorStatus::Failed;
    Revision revision = 0;
    std::vector<CrowdOverlayPrimitive> primitives;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief UI-neutral agent/zone/path document with reversible domain operations. */
class CrowdDocumentTarget final : public virtual IEditableTarget,
                                  public IDomainOperationTarget,
                                  public IDomainOperationTargetStaging {
public:
    /** @brief Stable capability id for crowd agent, zone and path editing. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.crowd-structure"); }
    explicit CrowdDocumentTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;

    /** @brief Enumerate agents in stable-id order. */
    std::vector<CrowdAgentRecord> agents() const;
    /** @brief Enumerate zones in stable-id order. */
    std::vector<CrowdZoneRecord> zones() const;
    /** @brief Enumerate paths in stable-id order. */
    std::vector<CrowdPathRecord> paths() const;
    /** @brief Plan reversible agent creation or replacement. */
    EditorResult<DomainOperation> makeSetAgent(const CrowdAgentRecord& record) const;
    /** @brief Plan reversible agent deletion. */
    EditorResult<DomainOperation> makeDeleteAgent(const StableId& id) const;
    /** @brief Plan reversible zone creation or replacement. */
    EditorResult<DomainOperation> makeSetZone(const CrowdZoneRecord& record) const;
    /** @brief Plan reversible zone deletion. */
    EditorResult<DomainOperation> makeDeleteZone(const StableId& id) const;
    /** @brief Plan reversible path creation or replacement. */
    EditorResult<DomainOperation> makeSetPath(const CrowdPathRecord& record) const;
    /** @brief Plan reversible path deletion when no agent references it. */
    EditorResult<DomainOperation> makeDeletePath(const StableId& id) const;
    /** @brief Validate geometry, numeric limits and cross references. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Produce deterministic path, zone and agent overlay primitives. */
    CrowdOverlayResult overlay(int primitiveBudget = 100000) const;
    /** @brief Capture deterministic schema-version-one authoring data. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load validated authoring data. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string id_;
    Revision revision_ = 1;
    EditRegion dirty_;
    std::map<StableId, CrowdAgentRecord> agents_;
    std::map<StableId, CrowdZoneRecord> zones_;
    std::map<StableId, CrowdPathRecord> paths_;
};

}  // namespace eve::editor

namespace eve::crowd { class Crowd; }

namespace eve::editor {

/** @brief Applies a complete crowd authoring snapshot to an isolated or live Crowd runtime. */
class CrowdRuntimeApplier {
public:
    /** @brief Replace runtime agents from a validated document; path agents seek their first waypoint. */
    EditorResult<void> apply(const CrowdDocumentTarget& document, crowd::Crowd* runtime) const;
};

}  // namespace eve::editor
