#include "ui_editing/UiDocument.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

namespace eve::ui_editing {
namespace {

template <class T>
EditorResult<T> uiError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

UiDocumentTarget::UiDocumentTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor UiDocumentTarget::describe() const {
    return {TargetId(id_), "ui-document", revision_, false,
            {IUiDocumentEditTarget::editorCapabilityId(),
             CapabilityId("eve.editor.target.ui-properties")}};
}

void* UiDocumentTarget::queryCapability(const CapabilityId& capability) {
    if (capability == IUiDocumentEditTarget::editorCapabilityId())
        return static_cast<IUiDocumentEditTarget*>(this);
    if (capability == CapabilityId("eve.editor.target.ui-properties"))
        return static_cast<IPropertyProvider*>(this);
    return nullptr;
}

EditorResult<void> UiDocumentTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return uiError<void>(EditorStatus::Rejected, "editor.ui.target-mismatch",
                             "UI operation targets another document");
    if (operation.type == "ui.widget.create.v1") {
        auto parsed = parseWidget(operation.payload);
        if (!parsed.isAccepted() || !parsed.value)
            return uiError<void>(EditorStatus::Rejected, "editor.ui.create-payload",
                                 "UI create payload is invalid");
        const UiWidgetSnapshot& value = *parsed.value;
        if (value.id.empty() || widgets_.contains(value.id))
            return uiError<void>(EditorStatus::Conflict, "editor.ui.widget-exists",
                                 "UI widget id is empty or already exists");
        if (!value.parent.empty() && !widgets_.contains(value.parent))
            return uiError<void>(EditorStatus::NotFound, "editor.ui.parent-not-found",
                                 "UI widget parent does not exist");
        widgets_.emplace(value.id, value);
    } else if (operation.type == "ui.widget.delete.v1") {
        auto parsed = parseWidget(operation.payload);
        if (!parsed.isAccepted() || !parsed.value || !widgets_.contains(parsed.value->id))
            return uiError<void>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                 "UI widget does not exist");
        if (!children(parsed.value->id).empty())
            return uiError<void>(EditorStatus::Rejected, "editor.ui.widget-has-children",
                                 "UI widget with children cannot be deleted");
        widgets_.erase(parsed.value->id);
    } else if (operation.type == "ui.widget.replace.v1") {
        auto parsed = parseWidget(operation.payload);
        if (!parsed.isAccepted() || !parsed.value)
            return uiError<void>(EditorStatus::Rejected, "editor.ui.replace-payload",
                                 "UI replacement payload is invalid");
        auto current = widgets_.find(parsed.value->id);
        if (current == widgets_.end())
            return uiError<void>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                 "UI widget does not exist");
        if (!parsed.value->parent.empty() && !widgets_.contains(parsed.value->parent))
            return uiError<void>(EditorStatus::NotFound, "editor.ui.parent-not-found",
                                 "UI widget parent does not exist");
        if (wouldCycle(parsed.value->id, parsed.value->parent, widgets_))
            return uiError<void>(EditorStatus::Rejected, "editor.ui.hierarchy-cycle",
                                 "UI reparenting would create a hierarchy cycle");
        current->second = *parsed.value;
    } else if (operation.type == "ui.widget.multi-replace.v1") {
        const auto* entries = operation.payload.getIf<EditorValue::Array>();
        if (!entries)
            return uiError<void>(EditorStatus::Rejected, "editor.ui.multi-payload",
                                 "UI multi-edit requires an array payload");
        auto candidate = widgets_;
        for (const EditorValue& entry : *entries) {
            auto parsed = parseWidget(entry);
            if (!parsed.isAccepted() || !parsed.value || !candidate.contains(parsed.value->id))
                return uiError<void>(EditorStatus::Rejected, "editor.ui.multi-widget",
                                     "UI multi-edit references an invalid widget");
            candidate[parsed.value->id] = *parsed.value;
        }
        for (const auto& [widgetId, value] : candidate) {
            if ((!value.parent.empty() && !candidate.contains(value.parent)) ||
                wouldCycle(widgetId, value.parent, candidate))
                return uiError<void>(EditorStatus::Rejected, "editor.ui.multi-hierarchy",
                                     "UI multi-edit produces an invalid hierarchy");
        }
        widgets_ = std::move(candidate);
    } else {
        return uiError<void>(EditorStatus::Unsupported, "editor.ui.operation-unsupported",
                             "Unsupported UI operation: " + operation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

std::unique_ptr<IDomainOperationTarget> UiDocumentTarget::cloneDomainState() const {
    return std::make_unique<UiDocumentTarget>(*this);
}

EditorResult<void> UiDocumentTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* document = dynamic_cast<UiDocumentTarget*>(candidate.get());
    if (!document || document->id_ != id_)
        return uiError<void>(EditorStatus::Conflict, "editor.ui.candidate-mismatch",
                             "UI compensation candidate belongs to another document");
    *this = *document;
    return EditorResult<void>::applied();
}

EditorResult<UiWidgetSnapshot> UiDocumentTarget::widget(const ObjectId& id) const {
    const auto found = widgets_.find(id);
    if (found == widgets_.end())
        return uiError<UiWidgetSnapshot>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                         "UI widget does not exist: " + id.value());
    return EditorResult<UiWidgetSnapshot>::applied(found->second);
}

std::vector<ObjectId> UiDocumentTarget::children(const ObjectId& parent) const {
    std::vector<ObjectId> result;
    for (const auto& [id, value] : widgets_)
        if (value.parent == parent) result.push_back(id);
    return result;
}

EditorResult<DomainOperation> UiDocumentTarget::makeCreate(const CreateUiWidgetRequest& request) const {
    if (request.id.empty() || request.type.empty() || request.name.empty())
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.invalid-widget",
                                        "UI widget id, type and name are required");
    if (widgets_.contains(request.id))
        return uiError<DomainOperation>(EditorStatus::Conflict, "editor.ui.widget-exists",
                                        "UI widget already exists: " + request.id.value());
    if (!request.parent.empty() && !widgets_.contains(request.parent))
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.parent-not-found",
                                        "UI widget parent does not exist");
    UiWidgetSnapshot value{request.id, request.parent, request.type, request.name, "", true, true,
                           request.layout, {}, {}};
    DomainOperation operation;
    operation.type = "ui.widget.create.v1";
    operation.inverseType = "ui.widget.delete.v1";
    operation.target = TargetId(id_);
    operation.payload = widgetValue(value);
    operation.inverse = operation.payload;
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), request.id.value(), 0});
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> UiDocumentTarget::makeDelete(const ObjectId& id) const {
    auto current = widget(id);
    if (!current.isAccepted() || !current.value)
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                        "UI widget does not exist");
    if (!children(id).empty())
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.widget-has-children",
                                        "Delete or reparent children before deleting their parent");
    DomainOperation operation;
    operation.type = "ui.widget.delete.v1";
    operation.inverseType = "ui.widget.create.v1";
    operation.target = TargetId(id_);
    operation.payload = widgetValue(*current.value);
    operation.inverse = operation.payload;
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), id.value(), 0});
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> UiDocumentTarget::makeRename(const ObjectId& id,
                                                            const std::string& name) const {
    auto current = widget(id);
    if (!current.isAccepted() || !current.value)
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                        "UI widget does not exist");
    if (name.empty())
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.empty-name",
                                        "UI widget name must not be empty");
    UiWidgetSnapshot changed = *current.value;
    changed.name = name;
    return makeReplace(*current.value, std::move(changed), "widget.name");
}

EditorResult<DomainOperation> UiDocumentTarget::makeReparent(const ObjectId& id,
                                                              const ObjectId& parent) const {
    auto current = widget(id);
    if (!current.isAccepted() || !current.value)
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                        "UI widget does not exist");
    if (!parent.empty() && !widgets_.contains(parent))
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.parent-not-found",
                                        "UI widget parent does not exist");
    if (wouldCycle(id, parent, widgets_))
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.hierarchy-cycle",
                                        "UI reparenting would create a hierarchy cycle");
    UiWidgetSnapshot changed = *current.value;
    changed.parent = parent;
    return makeReplace(*current.value, std::move(changed), "widget.parent");
}

EditorResult<DomainOperation> UiDocumentTarget::makeSetLayout(const ObjectId& id,
                                                               const UiLayoutValue& layout) const {
    auto current = widget(id);
    if (!current.isAccepted() || !current.value)
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                        "UI widget does not exist");
    if (layout.width < 0.0 || layout.height < 0.0 || layout.anchorX < 0.0 ||
        layout.anchorX > 1.0 || layout.anchorY < 0.0 || layout.anchorY > 1.0 ||
        layout.pivotX < 0.0 || layout.pivotX > 1.0 || layout.pivotY < 0.0 || layout.pivotY > 1.0)
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.invalid-layout",
                                        "UI size must be non-negative and anchors/pivots must be within 0..1");
    UiWidgetSnapshot changed = *current.value;
    changed.layout = layout;
    return makeReplace(*current.value, std::move(changed), "layout");
}

EditorResult<DomainOperation> UiDocumentTarget::makeSetStyle(const ObjectId& id,
                                                              const UiStyleValue& style) const {
    auto current = widget(id);
    if (!current.isAccepted() || !current.value)
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                        "UI widget does not exist");
    const double values[]{style.marginLeft, style.marginTop, style.marginRight, style.marginBottom,
                          style.paddingLeft, style.paddingTop, style.paddingRight, style.paddingBottom,
                          style.tintR, style.tintG, style.tintB, style.tintA, style.cornerRadius,
                          style.gap, style.flexGrow};
    for (double value : values)
        if (!std::isfinite(value))
            return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.nonfinite-style",
                                            "UI style values must be finite");
    const std::set<std::string> directions{"row", "column"};
    const std::set<std::string> aligns{"start", "center", "end", "stretch"};
    const std::set<std::string> justifies{"start", "center", "end", "space-between", "space-around"};
    if (style.marginLeft < 0.0 || style.marginTop < 0.0 || style.marginRight < 0.0 ||
        style.marginBottom < 0.0 || style.paddingLeft < 0.0 || style.paddingTop < 0.0 ||
        style.paddingRight < 0.0 || style.paddingBottom < 0.0 || style.cornerRadius < 0.0 ||
        style.gap < 0.0 || style.flexGrow < 0.0 || style.tintR < 0.0 || style.tintR > 1.0 ||
        style.tintG < 0.0 || style.tintG > 1.0 || style.tintB < 0.0 || style.tintB > 1.0 ||
        style.tintA < 0.0 || style.tintA > 1.0 || !directions.contains(style.direction) ||
        !aligns.contains(style.align) || !justifies.contains(style.justify))
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.invalid-style",
                                        "UI style contains invalid box, color or flex values");
    UiWidgetSnapshot changed = *current.value;
    changed.style = style;
    return makeReplace(*current.value, std::move(changed), "style");
}

EditorResult<DomainOperation> UiDocumentTarget::makeSetContent(const ObjectId& id,
                                                                const UiContentValue& content) const {
    auto current = widget(id);
    if (!current.isAccepted() || !current.value)
        return uiError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                        "UI widget does not exist");
    const double colors[]{content.textR, content.textG, content.textB, content.textA};
    const std::set<std::string> aligns{"start", "center", "end"};
    const std::set<std::string> fits{"stretch", "contain", "cover"};
    if (!std::isfinite(content.fontSize) || content.fontSize <= 0.0 || content.fontSize > 512.0 ||
        std::any_of(std::begin(colors), std::end(colors), [](double value) {
            return !std::isfinite(value) || value < 0.0 || value > 1.0;
        }) || !aligns.contains(content.horizontalAlign) || !aligns.contains(content.verticalAlign) ||
        !fits.contains(content.imageFit))
        return uiError<DomainOperation>(EditorStatus::Rejected, "editor.ui.invalid-content",
            "UI content contains an invalid font size, color, alignment or image fit");
    UiWidgetSnapshot changed = *current.value;
    changed.content = content;
    return makeReplace(*current.value, std::move(changed), "content");
}

std::vector<EditorDiagnostic> UiDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    int roots = 0;
    for (const auto& [id, value] : widgets_) {
        if (value.parent.empty()) ++roots;
        if (value.layout.width == 0.0 || value.layout.height == 0.0)
            diagnostics.push_back({RuleId("editor.ui.zero-size-widget"), DiagnosticSeverity::Warning,
                                   "Widget has zero preview size: " + id.value()});
        if (value.style.cornerRadius * 2.0 > std::min(value.layout.width, value.layout.height) &&
            value.layout.width > 0.0 && value.layout.height > 0.0)
            diagnostics.push_back({RuleId("editor.ui.excessive-corner-radius"), DiagnosticSeverity::Warning,
                                   "Corner radius exceeds half of widget size: " + id.value()});
        if (!value.text.empty() && value.content.fontAsset.empty())
            diagnostics.push_back({RuleId("editor.ui.missing-font"), DiagnosticSeverity::Warning,
                                   "Text widget has no font asset: " + id.value()});
    }
    if (!widgets_.empty() && roots == 0)
        diagnostics.push_back({RuleId("editor.ui.missing-root"), DiagnosticSeverity::Error,
                               "UI document has no root widget"});
    if (roots > 1)
        diagnostics.push_back({RuleId("editor.ui.multiple-roots"), DiagnosticSeverity::Warning,
                               "UI document contains multiple root widgets"});
    return diagnostics;
}

EditorResult<DomainOperation> UiDocumentTarget::makeReplace(const UiWidgetSnapshot& before,
                                                             UiWidgetSnapshot after,
                                                             std::string property) const {
    DomainOperation operation;
    operation.type = "ui.widget.replace.v1";
    operation.target = TargetId(id_);
    operation.payload = widgetValue(after);
    operation.inverse = widgetValue(before);
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), before.id.value(), 0});
    if (!property.empty()) operation.affectedProperties.push_back(std::move(property));
    operation.mergeKey = "ui.widget:" + before.id.value() + ":" +
                         (operation.affectedProperties.empty() ? "replace" : operation.affectedProperties.front());
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

bool UiDocumentTarget::wouldCycle(const ObjectId& id, const ObjectId& parent,
                                  const std::map<ObjectId, UiWidgetSnapshot>& widgets) const {
    ObjectId ancestor = parent;
    while (!ancestor.empty()) {
        if (ancestor == id) return true;
        const auto current = widgets.find(ancestor);
        if (current == widgets.end()) return false;
        ancestor = current->second.parent;
    }
    return false;
}

}  // namespace eve::ui_editing
