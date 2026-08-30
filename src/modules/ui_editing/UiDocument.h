#pragma once

#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "editing/EditableTarget.h"

#include <map>
#include <string>
#include <vector>

namespace eve::ui {
class UIHost;
}

namespace eve::ui_editing {

using editing::CapabilityId; using editing::DiagnosticSeverity; using editing::DomainOperation;
using editing::EditRegion; using editing::IDomainOperationTarget; using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget; using editing::IPropertyProvider; using editing::ObjectId;
using editing::PropertyDescriptor; using editing::PropertyFlag; using editing::PropertyPath;
using editing::PropertyReadResult; using editing::PropertyReadState; using editing::PropertySchema;
using editing::PropertySetMode; using editing::PropertyType; using editing::Revision; using editing::RuleId;
using editing::SelectionItem; using editing::SelectionSnapshot; using editing::TargetDescriptor; using editing::TargetId;
template <class T> using EditorResult = editing::Result<T>;
using EditorStatus = editing::Status; using EditorValue = editing::Value; using EditorDiagnostic = editing::Diagnostic;

/** @brief Renderer-neutral layout values shared by UI document hosts. */
struct UiLayoutValue {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    double pivotX = 0.0;
    double pivotY = 0.0;

    auto operator<=>(const UiLayoutValue&) const = default;
};

/** @brief Renderer-neutral visual and box-model style authored per widget. */
struct UiStyleValue {
    double marginLeft = 0.0, marginTop = 0.0, marginRight = 0.0, marginBottom = 0.0;
    double paddingLeft = 0.0, paddingTop = 0.0, paddingRight = 0.0, paddingBottom = 0.0;
    double tintR = 1.0, tintG = 1.0, tintB = 1.0, tintA = 1.0;
    double cornerRadius = 0.0;
    double gap = 0.0;
    double flexGrow = 0.0;
    std::string direction = "row";
    std::string align = "start";
    std::string justify = "start";

    auto operator<=>(const UiStyleValue&) const = default;
};

/** @brief Renderer-neutral text and image skin authored per widget. */
struct UiContentValue {
    std::string fontAsset;
    double fontSize = 16.0;
    double textR = 1.0, textG = 1.0, textB = 1.0, textA = 1.0;
    std::string horizontalAlign = "start";
    std::string verticalAlign = "start";
    std::string textureAsset;
    std::string imageFit = "stretch";
    bool clip = true;

    auto operator<=>(const UiContentValue&) const = default;
};

/** @brief Immutable widget snapshot used across editor/runtime boundaries. */
struct UiWidgetSnapshot {
    ObjectId id;
    ObjectId parent;
    std::string type;
    std::string name;
    std::string text;
    bool visible = true;
    bool enabled = true;
    UiLayoutValue layout;
    UiStyleValue style;
    UiContentValue content;
};

/** @brief Request for creating one widget with stable identity. */
struct CreateUiWidgetRequest {
    ObjectId id;
    ObjectId parent;
    std::string type;
    std::string name;
    UiLayoutValue layout;
};

/** @brief Hierarchy/layout capability consumed by UI tree and canvas tools. */
class IUiDocumentEditTarget {
public:
    virtual ~IUiDocumentEditTarget() = default;
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.ui-document"); }
    virtual EditorResult<UiWidgetSnapshot> widget(const ObjectId& id) const = 0;
    virtual std::vector<ObjectId> children(const ObjectId& parent) const = 0;
    virtual EditorResult<DomainOperation> makeCreate(const CreateUiWidgetRequest& request) const = 0;
    virtual EditorResult<DomainOperation> makeDelete(const ObjectId& id) const = 0;
    virtual EditorResult<DomainOperation> makeRename(const ObjectId& id, const std::string& name) const = 0;
    virtual EditorResult<DomainOperation> makeReparent(const ObjectId& id, const ObjectId& parent) const = 0;
    virtual EditorResult<DomainOperation> makeSetLayout(const ObjectId& id,
                                                        const UiLayoutValue& layout) const = 0;
    /** @brief Plan replacement of renderer-neutral widget style. */
    virtual EditorResult<DomainOperation> makeSetStyle(const ObjectId& id,
                                                       const UiStyleValue& style) const = 0;
    /** @brief Plan replacement of text/font/texture content skin. */
    virtual EditorResult<DomainOperation> makeSetContent(const ObjectId& id,
                                                         const UiContentValue& content) const = 0;
};

/** @brief Serializable UI authoring document with hierarchy and inspector capabilities. */
class UiDocumentTarget final : public virtual IEditableTarget,
                               public IDomainOperationTarget,
                               public IDomainOperationTargetStaging,
                               public IUiDocumentEditTarget,
                               public IPropertyProvider {
public:
    explicit UiDocumentTarget(std::string id);

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clone an isolated owning candidate for atomic compensation. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Publish a validated candidate belonging to this document. */
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    EditorResult<UiWidgetSnapshot> widget(const ObjectId& id) const override;
    std::vector<ObjectId> children(const ObjectId& parent) const override;
    EditorResult<DomainOperation> makeCreate(const CreateUiWidgetRequest& request) const override;
    EditorResult<DomainOperation> makeDelete(const ObjectId& id) const override;
    EditorResult<DomainOperation> makeRename(const ObjectId& id, const std::string& name) const override;
    EditorResult<DomainOperation> makeReparent(const ObjectId& id, const ObjectId& parent) const override;
    EditorResult<DomainOperation> makeSetLayout(const ObjectId& id,
                                                const UiLayoutValue& layout) const override;
    EditorResult<DomainOperation> makeSetStyle(const ObjectId& id,
                                               const UiStyleValue& style) const override;
    EditorResult<DomainOperation> makeSetContent(const ObjectId& id,
                                                 const UiContentValue& content) const override;

    /** @brief Validate style, hierarchy and viewport layout constraints. */
    std::vector<EditorDiagnostic> validate() const;

    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Capture deterministic UI document content. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load and validate a persisted UI document. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    static EditorValue layoutValue(const UiLayoutValue& layout);
    static EditorResult<UiLayoutValue> parseLayout(const EditorValue& value);
    static EditorValue styleValue(const UiStyleValue& style);
    static EditorResult<UiStyleValue> parseStyle(const EditorValue& value);
    static EditorValue contentValue(const UiContentValue& content);
    static EditorResult<UiContentValue> parseContent(const EditorValue& value);
    static EditorValue widgetValue(const UiWidgetSnapshot& widget);
    static EditorResult<UiWidgetSnapshot> parseWidget(const EditorValue& value);
    EditorResult<DomainOperation> makeReplace(const UiWidgetSnapshot& before,
                                              UiWidgetSnapshot after,
                                              std::string property = {}) const;
    bool wouldCycle(const ObjectId& id, const ObjectId& parent,
                    const std::map<ObjectId, UiWidgetSnapshot>& widgets) const;

    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<ObjectId, UiWidgetSnapshot> widgets_;
};

/** @brief Computed widget rectangle in a revision-bound UI preview. */
struct UiPreviewWidget {
    ObjectId id;
    ObjectId parent;
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    int depth = 0;
    bool visible = true;
};

/** @brief Deterministic UI layout preview used by canvas rendering and picking. */
struct UiPreviewSnapshot {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    double viewportWidth = 0.0, viewportHeight = 0.0;
    std::vector<UiPreviewWidget> widgets;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief UI-neutral preview, picking and anchor-gizmo geometry service. */
class UiDocumentPreviewService {
public:
    /** @brief Compute absolute rectangles for a viewport without mutating runtime UI. */
    UiPreviewSnapshot build(const UiDocumentTarget& document, double width, double height) const;
    /** @brief Pick the deepest visible widget under a preview-space point. */
    EditorResult<ObjectId> pick(const UiPreviewSnapshot& preview, double x, double y) const;
    /** @brief Return anchor point, pivot point and connecting line as numeric pairs. */
    EditorResult<EditorValue> anchorGizmo(const UiDocumentTarget& document,
                                          const UiPreviewSnapshot& preview,
                                          const ObjectId& widget) const;
};

/** @brief Optional bridge publishing a UI document into a real runtime UIHost. */
class UiDocumentRuntimeBridge {
public:
    /** @brief Reconcile a document subtree into a runtime host. */
    EditorResult<void> publish(const UiDocumentTarget& document, const ObjectId& root,
                               ui::UIHost* host) const;
};

}  // namespace eve::ui_editing
