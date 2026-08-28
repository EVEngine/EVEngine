#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorGizmoPreview.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"
#include "housegen/HouseGenTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::housegen { class HouseComponentLibrary; class HouseLayout; }

namespace eve::editor {

/** @brief Authored house component with stable editor identity. */
struct HouseKitComponentValue {
    ObjectId id;
    housegen::HouseComponent component;
};

/** @brief Revisioned component-kit and deterministic generation-request asset. */
class HouseGenDocumentTarget final : public IEditableTargetV2,
                                     public IDomainOperationTarget,
                                     public IDomainOperationTargetStaging,
                                     public IPropertyProvider {
public:
    explicit HouseGenDocumentTarget(std::string id);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
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
    /** @brief Add one component with stable editor and unique runtime IDs. */
    EditorResult<DomainOperation> makeCreateComponent(const HouseKitComponentValue& component) const;
    /** @brief Remove one component. */
    EditorResult<DomainOperation> makeDeleteComponent(const ObjectId& id) const;
    /** @brief Replace the deterministic generation request. */
    EditorResult<DomainOperation> makeSetRequest(const housegen::HouseRequest& request) const;
    const std::vector<HouseKitComponentValue>& components() const { return components_; }
    const housegen::HouseRequest& request() const { return request_; }
    /** @brief Validate kit fields, generator-required categories and request budgets. */
    std::vector<EditorDiagnostic> validate() const;
    EditorValue snapshotValue() const;
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
private:
    bool matches(const SelectionSnapshot& selection) const;
    EditorValue contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue content, std::string property = {}) const;
    std::string id_; Revision revision_ = 1; EditRegion dirty_;
    std::vector<HouseKitComponentValue> components_; housegen::HouseRequest request_;
};

/** @brief Candidate generation containing a validated library and deterministic layout. */
class HouseGenPreviewRuntime {
public:
    HouseGenPreviewRuntime();
    ~HouseGenPreviewRuntime();
    /** @brief Generate entirely in temporary state before replacing the active preview. */
    EditorResult<void> publish(const HouseGenDocumentTarget& document);
    /** @brief Build grid cells, component bounds and room overlays for the active generation. */
    EditorResult<EditorGizmoSnapshot> gizmo(Revision expectedRevision) const;
    const housegen::HouseLayout* layout() const { return layout_.get(); }
    const housegen::HouseComponentLibrary* library() const { return library_.get(); }
    Revision revision() const { return revision_; }
private:
    std::unique_ptr<housegen::HouseComponentLibrary> library_;
    std::unique_ptr<housegen::HouseLayout> layout_;
    Revision revision_ = 0;
};

} // namespace eve::editor
