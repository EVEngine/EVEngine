#pragma once

#include "editing/EditingAuthority.h"
#include "editing/EditingGizmo.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace eve::camera { class CameraController; }
namespace eve::graphics { class Camera3D; }

namespace eve::camera_editing {

using CapabilityId=editing::CapabilityId;using DiagnosticSeverity=editing::DiagnosticSeverity;using DomainOperation=editing::DomainOperation;
using EditRegion=editing::EditRegion;using EditorDiagnostic=editing::Diagnostic;template<class T>using EditorResult=editing::Result<T>;
using EditorStatus=editing::Status;using EditorValue=editing::Value;using IDomainOperationTarget=editing::IDomainOperationTarget;
using IDomainOperationTargetStaging=editing::IDomainOperationTargetStaging;using IEditableTarget=editing::IEditableTarget;
using IPropertyProvider=editing::IPropertyProvider;using ObjectId=editing::ObjectId;using PropertyDescriptor=editing::PropertyDescriptor;
using PropertyFlag=editing::PropertyFlag;using PropertyPath=editing::PropertyPath;using PropertyReadResult=editing::PropertyReadResult;
using PropertyReadState=editing::PropertyReadState;using PropertySchema=editing::PropertySchema;using PropertySetMode=editing::PropertySetMode;
using PropertyType=editing::PropertyType;using Revision=editing::Revision;using RuleId=editing::RuleId;
using SelectionSnapshot=editing::SelectionSnapshot;using TargetDescriptor=editing::TargetDescriptor;using TargetId=editing::TargetId;
using EditorGizmoSnapshot=editing::GizmoSnapshot;using EditorGizmoPrimitive=editing::GizmoPrimitive;using editing::validatePropertyValue;

/** @brief Stable authored camera rig independent of a live scene camera. */
struct CameraRigValue {
    ObjectId id;
    std::string name;
    std::string mode = "follow";
    int priority = 0;
    bool enabled = true;
    glm::vec3 target{0.f};
    glm::vec3 offset{0.f, 2.f, 6.f};
    glm::vec3 lookAhead{0.f};
    glm::vec2 composition{0.f};
    float radius = 10.f;
    float azimuth = 45.f;
    float elevation = 30.f;
    float yaw = 0.f;
    float pitch = 0.f;
    float fov = 60.f;
    float smooth = 6.f;
    float maxSpeed = 0.f;
};

/** @brief Stable camera timeline key (cut, event, or scalar automation). */
struct CameraTimelineKeyValue {
    ObjectId id;
    std::string kind;
    float time = 0.f;
    ObjectId rig;
    float blend = 0.f;
    std::string property;
    float value = 0.f;
    std::string name;
    std::string data;
};

/** @brief Revisioned camera-rig and director-timeline editing document. */
class CameraDocumentTarget final : public virtual IEditableTarget,
                                   public IDomainOperationTarget,
                                   public IDomainOperationTargetStaging,
                                   public IPropertyProvider {
public:
    explicit CameraDocumentTarget(std::string id);
    TargetId targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
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
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Add a rig with a stable identity. */
    EditorResult<DomainOperation> makeCreateRig(const CameraRigValue& rig) const;
    /** @brief Delete a rig only when no timeline cut references it. */
    EditorResult<DomainOperation> makeDeleteRig(const ObjectId& id) const;
    /** @brief Add a stable timeline key. */
    EditorResult<DomainOperation> makeCreateKey(const CameraTimelineKeyValue& key) const;
    /** @brief Remove a timeline key. */
    EditorResult<DomainOperation> makeDeleteKey(const ObjectId& id) const;
    /** @brief Return authored rigs in stable document order. */
    const std::vector<CameraRigValue>& rigs() const { return rigs_; }
    /** @brief Return timeline keys sorted by time then stable identity. */
    std::vector<CameraTimelineKeyValue> timeline() const;
    /** @brief Validate references, ranges, names and document budgets. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Capture a versioned atomic persistence value. */
    EditorValue snapshotValue() const;
    /** @brief Atomically replace the document from a snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
private:
    bool matches(const SelectionSnapshot& selection) const;
    EditorValue contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue content, std::string property = {}) const;
    std::string id_; Revision revision_ = 1; EditRegion dirty_;
    std::vector<CameraRigValue> rigs_; std::vector<CameraTimelineKeyValue> keys_;
};

/** @brief Renderer-neutral camera pose sampled from an authored timeline. */
struct CameraPreviewPose { glm::vec3 eye{0.f}; glm::vec3 target{0.f}; float fov = 60.f; ObjectId rig; };

/** @brief Deterministic scrub evaluator and camera/frustum gizmo producer. */
class CameraPreview {
public:
    /** @brief Evaluate the most recent cut and scalar keys at a bounded time. */
    EditorResult<CameraPreviewPose> evaluate(const CameraDocumentTarget& document, float time) const;
    /** @brief Build camera position, look line, target and near-plane overlay primitives. */
    EditorResult<EditorGizmoSnapshot> gizmo(const CameraDocumentTarget& document, float time) const;
};

/** @brief Candidate-first bridge from camera assets to CameraController. */
class CameraDocumentRuntime {
public:
    CameraDocumentRuntime();
    ~CameraDocumentRuntime();
    /** @brief Build a complete controller before replacing the active generation. */
    EditorResult<void> publish(const CameraDocumentTarget& document, graphics::Camera3D* camera);
    /** @brief Access the active controller generation. @return Borrowed pointer owned by this runtime. @lifetime Valid until the next successful publish or runtime destruction. */
    camera::CameraController* controller() const { return controller_.get(); }
    Revision revision() const { return revision_; }
private:
    std::unique_ptr<camera::CameraController> controller_; Revision revision_ = 0;
};

} // namespace eve::camera_editing
