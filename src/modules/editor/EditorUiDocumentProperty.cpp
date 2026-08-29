#include "editor/EditorUiDocumentTarget.h"

#include <optional>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> propertyError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

PropertyDescriptor uiProperty(const char* path, const char* label, PropertyType type,
                              EditorValue defaultValue, const char* category) {
    PropertyDescriptor result;
    result.path = PropertyPath(path);
    result.displayNameKey = label;
    result.category = category;
    result.type = type;
    result.flags = PropertyFlag::Runtime | PropertyFlag::MultiEdit;
    result.defaultValue = std::move(defaultValue);
    return result;
}

EditorValue component(const UiWidgetSnapshot& widget, const PropertyPath& path) {
    if (path == PropertyPath("widget.name")) return widget.name;
    if (path == PropertyPath("widget.text")) return widget.text;
    if (path == PropertyPath("content.fontAsset")) return widget.content.fontAsset;
    if (path == PropertyPath("content.fontSize")) return widget.content.fontSize;
    if (path == PropertyPath("content.textColor"))
        return EditorValue::Array{widget.content.textR, widget.content.textG,
                                  widget.content.textB, widget.content.textA};
    if (path == PropertyPath("content.horizontalAlign")) return widget.content.horizontalAlign;
    if (path == PropertyPath("content.verticalAlign")) return widget.content.verticalAlign;
    if (path == PropertyPath("content.textureAsset")) return widget.content.textureAsset;
    if (path == PropertyPath("content.imageFit")) return widget.content.imageFit;
    if (path == PropertyPath("content.clip")) return widget.content.clip;
    if (path == PropertyPath("widget.visible")) return widget.visible;
    if (path == PropertyPath("widget.enabled")) return widget.enabled;
    if (path == PropertyPath("layout.position")) return EditorValue::Array{widget.layout.x, widget.layout.y};
    if (path == PropertyPath("layout.size"))
        return EditorValue::Array{widget.layout.width, widget.layout.height};
    if (path == PropertyPath("layout.anchor"))
        return EditorValue::Array{widget.layout.anchorX, widget.layout.anchorY};
    if (path == PropertyPath("layout.pivot"))
        return EditorValue::Array{widget.layout.pivotX, widget.layout.pivotY};
    if (path == PropertyPath("style.margin"))
        return EditorValue::Array{widget.style.marginLeft, widget.style.marginTop,
                                  widget.style.marginRight, widget.style.marginBottom};
    if (path == PropertyPath("style.padding"))
        return EditorValue::Array{widget.style.paddingLeft, widget.style.paddingTop,
                                  widget.style.paddingRight, widget.style.paddingBottom};
    if (path == PropertyPath("style.tint"))
        return EditorValue::Array{widget.style.tintR, widget.style.tintG,
                                  widget.style.tintB, widget.style.tintA};
    if (path == PropertyPath("style.cornerRadius")) return widget.style.cornerRadius;
    if (path == PropertyPath("style.gap")) return widget.style.gap;
    if (path == PropertyPath("style.flexGrow")) return widget.style.flexGrow;
    if (path == PropertyPath("style.direction")) return widget.style.direction;
    if (path == PropertyPath("style.align")) return widget.style.align;
    if (path == PropertyPath("style.justify")) return widget.style.justify;
    return {};
}

bool assign(UiWidgetSnapshot& widget, const PropertyPath& path, const EditorValue& value) {
    if (path == PropertyPath("content.fontAsset") || path == PropertyPath("content.textureAsset")) {
        const auto* asset = value.getIf<std::string>(); if (!asset) return false;
        if (path == PropertyPath("content.fontAsset")) widget.content.fontAsset = *asset;
        else widget.content.textureAsset = *asset;
        return true;
    }
    if (path == PropertyPath("content.fontSize")) {
        const auto* number = value.getIf<double>();
        if (!number || *number <= 0.0 || *number > 512.0) return false;
        widget.content.fontSize = *number; return true;
    }
    if (path == PropertyPath("content.clip")) {
        const auto* flag = value.getIf<bool>(); if (!flag) return false;
        widget.content.clip = *flag; return true;
    }
    if (path == PropertyPath("content.horizontalAlign") ||
        path == PropertyPath("content.verticalAlign") || path == PropertyPath("content.imageFit")) {
        const auto* text = value.getIf<std::string>(); if (!text) return false;
        if (path == PropertyPath("content.imageFit")) {
            if (*text != "stretch" && *text != "contain" && *text != "cover") return false;
            widget.content.imageFit = *text;
        } else {
            if (*text != "start" && *text != "center" && *text != "end") return false;
            if (path == PropertyPath("content.horizontalAlign")) widget.content.horizontalAlign = *text;
            else widget.content.verticalAlign = *text;
        }
        return true;
    }
    if (path == PropertyPath("widget.name") || path == PropertyPath("widget.text")) {
        const auto* text = value.getIf<std::string>();
        if (!text || (path == PropertyPath("widget.name") && text->empty())) return false;
        if (path == PropertyPath("widget.name")) widget.name = *text;
        else widget.text = *text;
        return true;
    }
    if (path == PropertyPath("widget.visible") || path == PropertyPath("widget.enabled")) {
        const auto* flag = value.getIf<bool>();
        if (!flag) return false;
        if (path == PropertyPath("widget.visible")) widget.visible = *flag;
        else widget.enabled = *flag;
        return true;
    }
    if (path == PropertyPath("style.cornerRadius") || path == PropertyPath("style.gap") ||
        path == PropertyPath("style.flexGrow")) {
        const auto* number = value.getIf<double>();
        if (!number || *number < 0.0) return false;
        if (path == PropertyPath("style.cornerRadius")) widget.style.cornerRadius = *number;
        else if (path == PropertyPath("style.gap")) widget.style.gap = *number;
        else widget.style.flexGrow = *number;
        return true;
    }
    if (path == PropertyPath("style.direction") || path == PropertyPath("style.align") ||
        path == PropertyPath("style.justify")) {
        const auto* text = value.getIf<std::string>();
        if (!text) return false;
        if (path == PropertyPath("style.direction")) {
            if (*text != "row" && *text != "column") return false;
            widget.style.direction = *text;
        } else if (path == PropertyPath("style.align")) {
            if (*text != "start" && *text != "center" && *text != "end" && *text != "stretch") return false;
            widget.style.align = *text;
        } else {
            if (*text != "start" && *text != "center" && *text != "end" &&
                *text != "space-between" && *text != "space-around") return false;
            widget.style.justify = *text;
        }
        return true;
    }
    const auto* tuple = value.getIf<EditorValue::Array>();
    if ((path == PropertyPath("style.margin") || path == PropertyPath("style.padding") ||
         path == PropertyPath("style.tint") || path == PropertyPath("content.textColor"))) {
        if (!tuple || tuple->size() != 4) return false;
        double values[4];
        for (int i = 0; i < 4; ++i) { const auto* number = (*tuple)[i].getIf<double>(); if (!number || *number < 0.0) return false; values[i] = *number; }
        if (path == PropertyPath("style.tint") || path == PropertyPath("content.textColor")) {
            for (double number : values) if (number > 1.0) return false;
            if (path == PropertyPath("style.tint")) {
                widget.style.tintR = values[0]; widget.style.tintG = values[1]; widget.style.tintB = values[2]; widget.style.tintA = values[3];
            } else {
                widget.content.textR = values[0]; widget.content.textG = values[1]; widget.content.textB = values[2]; widget.content.textA = values[3];
            }
        } else if (path == PropertyPath("style.margin")) {
            widget.style.marginLeft = values[0]; widget.style.marginTop = values[1]; widget.style.marginRight = values[2]; widget.style.marginBottom = values[3];
        } else {
            widget.style.paddingLeft = values[0]; widget.style.paddingTop = values[1]; widget.style.paddingRight = values[2]; widget.style.paddingBottom = values[3];
        }
        return true;
    }
    if (!tuple || tuple->size() != 2) return false;
    const auto* first = (*tuple)[0].getIf<double>();
    const auto* second = (*tuple)[1].getIf<double>();
    if (!first || !second) return false;
    if (path == PropertyPath("layout.position")) {
        widget.layout.x = *first;
        widget.layout.y = *second;
    } else if (path == PropertyPath("layout.size")) {
        if (*first < 0.0 || *second < 0.0) return false;
        widget.layout.width = *first;
        widget.layout.height = *second;
    } else if (path == PropertyPath("layout.anchor")) {
        if (*first < 0.0 || *first > 1.0 || *second < 0.0 || *second > 1.0) return false;
        widget.layout.anchorX = *first;
        widget.layout.anchorY = *second;
    } else if (path == PropertyPath("layout.pivot")) {
        if (*first < 0.0 || *first > 1.0 || *second < 0.0 || *second > 1.0) return false;
        widget.layout.pivotX = *first;
        widget.layout.pivotY = *second;
    } else {
        return false;
    }
    return true;
}

}  // namespace

eve::Result<eve::Revision> UiDocumentTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (selection.items.empty())
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "UI selection is empty", "editor.ui.selection", {},
            "editor.UiDocumentTarget"));
    for (const SelectionItem& item : selection.items)
        if (item.target != TargetId(id_) || !widgets_.contains(ObjectId(item.item.value())))
            return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "UI selection contains an invalid widget",
                "editor.ui.selection", {}, "editor.UiDocumentTarget"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema UiDocumentTarget::schema(const SelectionSnapshot&) const {
    PropertySchema result;
    result.typeId = "ui.widget";
    result.properties = {
        uiProperty("widget.name", "editor.ui.name", PropertyType::String, "Widget", "widget"),
        uiProperty("widget.text", "editor.ui.text", PropertyType::String, "", "content"),
        uiProperty("content.fontAsset", "editor.ui.fontAsset", PropertyType::AssetRef, "", "content"),
        uiProperty("content.fontSize", "editor.ui.fontSize", PropertyType::Float, 16.0, "content"),
        uiProperty("content.textColor", "editor.ui.textColor", PropertyType::Color,
                   EditorValue::Array{1.0, 1.0, 1.0, 1.0}, "content"),
        uiProperty("content.horizontalAlign", "editor.ui.horizontalAlign", PropertyType::Enum, "start", "content"),
        uiProperty("content.verticalAlign", "editor.ui.verticalAlign", PropertyType::Enum, "start", "content"),
        uiProperty("content.textureAsset", "editor.ui.textureAsset", PropertyType::AssetRef, "", "content"),
        uiProperty("content.imageFit", "editor.ui.imageFit", PropertyType::Enum, "stretch", "content"),
        uiProperty("content.clip", "editor.ui.clip", PropertyType::Bool, true, "content"),
        uiProperty("widget.visible", "editor.ui.visible", PropertyType::Bool, true, "widget"),
        uiProperty("widget.enabled", "editor.ui.enabled", PropertyType::Bool, true, "widget"),
        uiProperty("layout.position", "editor.ui.position", PropertyType::Vec2,
                   EditorValue::Array{0.0, 0.0}, "layout"),
        uiProperty("layout.size", "editor.ui.size", PropertyType::Vec2,
                   EditorValue::Array{0.0, 0.0}, "layout"),
        uiProperty("layout.anchor", "editor.ui.anchor", PropertyType::Vec2,
                   EditorValue::Array{0.0, 0.0}, "layout"),
        uiProperty("layout.pivot", "editor.ui.pivot", PropertyType::Vec2,
                   EditorValue::Array{0.0, 0.0}, "layout"),
        uiProperty("style.margin", "editor.ui.margin", PropertyType::Vec4,
                   EditorValue::Array{0.0, 0.0, 0.0, 0.0}, "style"),
        uiProperty("style.padding", "editor.ui.padding", PropertyType::Vec4,
                   EditorValue::Array{0.0, 0.0, 0.0, 0.0}, "style"),
        uiProperty("style.tint", "editor.ui.tint", PropertyType::Color,
                   EditorValue::Array{1.0, 1.0, 1.0, 1.0}, "style"),
        uiProperty("style.cornerRadius", "editor.ui.cornerRadius", PropertyType::Float, 0.0, "style"),
        uiProperty("style.gap", "editor.ui.gap", PropertyType::Float, 0.0, "flex"),
        uiProperty("style.flexGrow", "editor.ui.flexGrow", PropertyType::Float, 0.0, "flex"),
        uiProperty("style.direction", "editor.ui.direction", PropertyType::Enum, "row", "flex"),
        uiProperty("style.align", "editor.ui.align", PropertyType::Enum, "start", "flex"),
        uiProperty("style.justify", "editor.ui.justify", PropertyType::Enum, "start", "flex")};
    return result;
}

PropertyReadResult UiDocumentTarget::read(const SelectionSnapshot& selection,
                                          const PropertyPath& path) const {
    std::optional<EditorValue> common;
    for (const SelectionItem& item : selection.items) {
        if (item.target != TargetId(id_)) return {};
        const auto found = widgets_.find(ObjectId(item.item.value()));
        if (found == widgets_.end()) return {};
        EditorValue value = component(found->second, path);
        if (value.type() == EditorValue::Type::Null) return {};
        if (!common) common = value;
        else if (*common != value) return {PropertyReadState::Mixed, {}, {}};
    }
    return common ? PropertyReadResult{PropertyReadState::Value, *common, {}} : PropertyReadResult{};
}

EditorResult<DomainOperation> UiDocumentTarget::makeSet(const SelectionSnapshot& selection,
                                                         const PropertyPath& path,
                                                         const EditorValue& value,
                                                         PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute || selection.items.empty())
        return propertyError<DomainOperation>(EditorStatus::Unsupported, "editor.ui.property-mode",
                                              "UI properties require a non-empty absolute assignment");
    if (!schema(selection).find(path))
        return propertyError<DomainOperation>(EditorStatus::Unsupported, "editor.ui.property-path",
                                              "Unknown UI property: " + path.value());
    EditorValue::Array payload;
    EditorValue::Array inverse;
    DomainOperation operation;
    for (const SelectionItem& item : selection.items) {
        if (item.target != TargetId(id_))
            return propertyError<DomainOperation>(EditorStatus::Rejected, "editor.ui.property-target",
                                                  "UI selection spans another target");
        const auto found = widgets_.find(ObjectId(item.item.value()));
        if (found == widgets_.end())
            return propertyError<DomainOperation>(EditorStatus::NotFound, "editor.ui.widget-not-found",
                                                  "Selected UI widget does not exist");
        UiWidgetSnapshot changed = found->second;
        if (!assign(changed, path, value))
            return propertyError<DomainOperation>(EditorStatus::Rejected, "editor.ui.property-value",
                                                  "UI property value is invalid for " + path.value());
        payload.push_back(widgetValue(changed));
        inverse.push_back(widgetValue(found->second));
        operation.affectedObjects.push_back({TargetId(id_), item.item.value(), 0});
    }
    operation.type = "ui.widget.multi-replace.v1";
    operation.target = TargetId(id_);
    operation.payload = EditorValue(std::move(payload));
    operation.inverse = EditorValue(std::move(inverse));
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "ui.selection:" + path.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> UiDocumentTarget::makeReset(const SelectionSnapshot& selection,
                                                           const PropertyPath& path) const {
    auto property = schema(selection).find(path);
    if (!property)
        return propertyError<DomainOperation>(EditorStatus::Unsupported, "editor.ui.property-path",
                                              "Unknown UI property: " + path.value());
    return makeSet(selection, path, property->defaultValue, PropertySetMode::Absolute);
}

}  // namespace eve::editor
