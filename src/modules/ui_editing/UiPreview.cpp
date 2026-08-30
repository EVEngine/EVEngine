#include "ui_editing/UiDocument.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace eve::ui_editing {

UiPreviewSnapshot UiDocumentPreviewService::build(const UiDocumentTarget& document,
                                                   double width, double height) const {
    UiPreviewSnapshot result;
    result.documentRevision = document.revision();
    result.viewportWidth = width;
    result.viewportHeight = height;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
        result.diagnostics.push_back({RuleId("editor.ui.invalid-preview-viewport"),
                                      DiagnosticSeverity::Error,
                                      "UI preview viewport must have finite positive dimensions"});
        return result;
    }
    result.diagnostics = document.validate();
    std::map<ObjectId, UiPreviewWidget> computed;
    std::function<void(const ObjectId&, const UiPreviewWidget&, int)> visit;
    visit = [&](const ObjectId& id, const UiPreviewWidget& parent, int depth) {
        auto value = document.widget(id);
        if (!value.value) return;
        const UiWidgetSnapshot& widget = *value.value;
        const double contentX = parent.x + widget.style.marginLeft +
                                (depth == 0 ? 0.0 : 0.0);
        const double contentY = parent.y + widget.style.marginTop;
        const double parentWidth = std::max(0.0, parent.width - widget.style.marginLeft -
                                                  widget.style.marginRight);
        const double parentHeight = std::max(0.0, parent.height - widget.style.marginTop -
                                                   widget.style.marginBottom);
        UiPreviewWidget preview;
        preview.id = widget.id; preview.parent = widget.parent; preview.depth = depth;
        preview.width = widget.layout.width; preview.height = widget.layout.height;
        preview.x = contentX + parentWidth * widget.layout.anchorX + widget.layout.x -
                    preview.width * widget.layout.pivotX;
        preview.y = contentY + parentHeight * widget.layout.anchorY + widget.layout.y -
                    preview.height * widget.layout.pivotY;
        preview.visible = widget.visible && parent.visible;
        computed[id] = preview; result.widgets.push_back(preview);
        UiPreviewWidget childParent = preview;
        childParent.x += widget.style.paddingLeft; childParent.y += widget.style.paddingTop;
        childParent.width = std::max(0.0, childParent.width - widget.style.paddingLeft - widget.style.paddingRight);
        childParent.height = std::max(0.0, childParent.height - widget.style.paddingTop - widget.style.paddingBottom);
        for (const ObjectId& child : document.children(id)) visit(child, childParent, depth + 1);
    };
    const UiPreviewWidget viewport{ObjectId(), ObjectId(), 0.0, 0.0, width, height, -1, true};
    for (const ObjectId& root : document.children(ObjectId())) visit(root, viewport, 0);
    const bool hasError = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const EditorDiagnostic& diagnostic) { return diagnostic.severity == DiagnosticSeverity::Error; });
    result.status = hasError ? EditorStatus::Failed : EditorStatus::Applied;
    return result;
}

EditorResult<ObjectId> UiDocumentPreviewService::pick(const UiPreviewSnapshot& preview,
                                                       double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y))
        return EditorResult<ObjectId>::error(EditorStatus::Rejected,
            RuleId("editor.ui.invalid-pick-point"), "UI pick coordinates must be finite");
    const UiPreviewWidget* best = nullptr;
    for (const UiPreviewWidget& widget : preview.widgets) {
        if (!widget.visible || x < widget.x || y < widget.y || x > widget.x + widget.width ||
            y > widget.y + widget.height) continue;
        if (!best || widget.depth > best->depth ||
            (widget.depth == best->depth && widget.id > best->id)) best = &widget;
    }
    if (!best)
        return EditorResult<ObjectId>::error(EditorStatus::NotFound,
            RuleId("editor.ui.pick-miss"), "No visible widget contains the preview point");
    return EditorResult<ObjectId>::applied(best->id);
}

EditorResult<EditorValue> UiDocumentPreviewService::anchorGizmo(
    const UiDocumentTarget& document, const UiPreviewSnapshot& preview, const ObjectId& id) const {
    if (preview.documentRevision != document.revision())
        return EditorResult<EditorValue>::error(EditorStatus::Conflict,
            RuleId("editor.ui.stale-preview"), "UI preview revision no longer matches the document");
    auto widget = document.widget(id);
    if (!widget.value)
        return EditorResult<EditorValue>::error(EditorStatus::NotFound,
            RuleId("editor.ui.widget-not-found"), "UI widget does not exist");
    double parentX = 0.0, parentY = 0.0, parentW = preview.viewportWidth, parentH = preview.viewportHeight;
    if (!widget.value->parent.empty()) {
        const auto parent = std::find_if(preview.widgets.begin(), preview.widgets.end(),
            [&](const UiPreviewWidget& value) { return value.id == widget.value->parent; });
        if (parent == preview.widgets.end())
            return EditorResult<EditorValue>::error(EditorStatus::NotFound,
                RuleId("editor.ui.preview-parent-not-found"), "Parent is absent from UI preview");
        auto parentWidget = document.widget(parent->id);
        parentX = parent->x + parentWidget.value->style.paddingLeft;
        parentY = parent->y + parentWidget.value->style.paddingTop;
        parentW = std::max(0.0, parent->width - parentWidget.value->style.paddingLeft - parentWidget.value->style.paddingRight);
        parentH = std::max(0.0, parent->height - parentWidget.value->style.paddingTop - parentWidget.value->style.paddingBottom);
    }
    const auto previewWidget = std::find_if(preview.widgets.begin(), preview.widgets.end(),
        [&](const UiPreviewWidget& value) { return value.id == id; });
    if (previewWidget == preview.widgets.end())
        return EditorResult<EditorValue>::error(EditorStatus::NotFound,
            RuleId("editor.ui.preview-widget-not-found"), "Widget is absent from UI preview");
    const double anchorX = parentX + parentW * widget.value->layout.anchorX;
    const double anchorY = parentY + parentH * widget.value->layout.anchorY;
    const double pivotX = previewWidget->x + previewWidget->width * widget.value->layout.pivotX;
    const double pivotY = previewWidget->y + previewWidget->height * widget.value->layout.pivotY;
    return EditorResult<EditorValue>::applied(EditorValue::Object{
        {"anchor", EditorValue::Array{anchorX, anchorY}},
        {"pivot", EditorValue::Array{pivotX, pivotY}},
        {"line", EditorValue::Array{anchorX, anchorY, pivotX, pivotY}}});
}

}  // namespace eve::ui_editing
