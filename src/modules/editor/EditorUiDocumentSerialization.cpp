#include "editor/EditorUiDocumentTarget.h"

#include <cmath>
#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> serializationError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

const double* numberField(const EditorValue& value, const char* key) {
    const EditorValue* entry = field(value, key);
    return entry ? entry->getIf<double>() : nullptr;
}

}  // namespace

EditorValue UiDocumentTarget::layoutValue(const UiLayoutValue& layout) {
    EditorValue::Object value;
    value["x"] = layout.x;
    value["y"] = layout.y;
    value["width"] = layout.width;
    value["height"] = layout.height;
    value["anchorX"] = layout.anchorX;
    value["anchorY"] = layout.anchorY;
    value["pivotX"] = layout.pivotX;
    value["pivotY"] = layout.pivotY;
    return EditorValue(std::move(value));
}

EditorResult<UiLayoutValue> UiDocumentTarget::parseLayout(const EditorValue& value) {
    const double* x = numberField(value, "x");
    const double* y = numberField(value, "y");
    const double* width = numberField(value, "width");
    const double* height = numberField(value, "height");
    const double* anchorX = numberField(value, "anchorX");
    const double* anchorY = numberField(value, "anchorY");
    const double* pivotX = numberField(value, "pivotX");
    const double* pivotY = numberField(value, "pivotY");
    if (!x || !y || !width || !height || !anchorX || !anchorY || !pivotX || !pivotY ||
        *width < 0.0 || *height < 0.0 || *anchorX < 0.0 || *anchorX > 1.0 ||
        *anchorY < 0.0 || *anchorY > 1.0 || *pivotX < 0.0 || *pivotX > 1.0 ||
        *pivotY < 0.0 || *pivotY > 1.0)
        return serializationError<UiLayoutValue>(EditorStatus::Rejected, "editor.ui.invalid-layout",
                                                  "UI layout fields are missing or out of range");
    return EditorResult<UiLayoutValue>::applied(
        {*x, *y, *width, *height, *anchorX, *anchorY, *pivotX, *pivotY});
}

EditorValue UiDocumentTarget::styleValue(const UiStyleValue& style) {
    return EditorValue::Object{{"marginLeft", style.marginLeft}, {"marginTop", style.marginTop},
        {"marginRight", style.marginRight}, {"marginBottom", style.marginBottom},
        {"paddingLeft", style.paddingLeft}, {"paddingTop", style.paddingTop},
        {"paddingRight", style.paddingRight}, {"paddingBottom", style.paddingBottom},
        {"tintR", style.tintR}, {"tintG", style.tintG}, {"tintB", style.tintB},
        {"tintA", style.tintA}, {"cornerRadius", style.cornerRadius}, {"gap", style.gap},
        {"flexGrow", style.flexGrow}, {"direction", style.direction}, {"align", style.align},
        {"justify", style.justify}};
}

EditorResult<UiStyleValue> UiDocumentTarget::parseStyle(const EditorValue& value) {
    UiStyleValue style;
    double* outputs[]{&style.marginLeft, &style.marginTop, &style.marginRight, &style.marginBottom,
                      &style.paddingLeft, &style.paddingTop, &style.paddingRight, &style.paddingBottom,
                      &style.tintR, &style.tintG, &style.tintB, &style.tintA, &style.cornerRadius,
                      &style.gap, &style.flexGrow};
    const char* keys[]{"marginLeft", "marginTop", "marginRight", "marginBottom", "paddingLeft",
                       "paddingTop", "paddingRight", "paddingBottom", "tintR", "tintG", "tintB",
                       "tintA", "cornerRadius", "gap", "flexGrow"};
    for (std::size_t i = 0; i < std::size(outputs); ++i) {
        const double* number = numberField(value, keys[i]);
        if (!number || !std::isfinite(*number))
            return serializationError<UiStyleValue>(EditorStatus::Rejected, "editor.ui.invalid-style",
                                                     "UI style numeric fields are missing or invalid");
        *outputs[i] = *number;
    }
    const EditorValue* directionValue = field(value, "direction");
    const EditorValue* alignValue = field(value, "align");
    const EditorValue* justifyValue = field(value, "justify");
    const auto* direction = directionValue ? directionValue->getIf<std::string>() : nullptr;
    const auto* align = alignValue ? alignValue->getIf<std::string>() : nullptr;
    const auto* justify = justifyValue ? justifyValue->getIf<std::string>() : nullptr;
    if (!direction || !align || !justify)
        return serializationError<UiStyleValue>(EditorStatus::Rejected, "editor.ui.invalid-style",
                                                 "UI flex style fields are missing");
    style.direction = *direction; style.align = *align; style.justify = *justify;
    const std::set<std::string> directions{"row", "column"};
    const std::set<std::string> aligns{"start", "center", "end", "stretch"};
    const std::set<std::string> justifies{"start", "center", "end", "space-between", "space-around"};
    for (std::size_t i = 0; i < std::size(outputs); ++i)
        if (*outputs[i] < 0.0 || (i >= 8 && i <= 11 && *outputs[i] > 1.0))
            return serializationError<UiStyleValue>(EditorStatus::Rejected, "editor.ui.invalid-style-range",
                                                     "UI box values must be non-negative and tint normalized");
    if (!directions.contains(style.direction) || !aligns.contains(style.align) ||
        !justifies.contains(style.justify))
        return serializationError<UiStyleValue>(EditorStatus::Rejected, "editor.ui.invalid-flex-style",
                                                 "UI flex direction, alignment or justification is invalid");
    return EditorResult<UiStyleValue>::applied(std::move(style));
}

EditorValue UiDocumentTarget::contentValue(const UiContentValue& content) {
    return EditorValue::Object{{"fontAsset", content.fontAsset}, {"fontSize", content.fontSize},
        {"textR", content.textR}, {"textG", content.textG}, {"textB", content.textB},
        {"textA", content.textA}, {"horizontalAlign", content.horizontalAlign},
        {"verticalAlign", content.verticalAlign}, {"textureAsset", content.textureAsset},
        {"imageFit", content.imageFit}, {"clip", content.clip}};
}

EditorResult<UiContentValue> UiDocumentTarget::parseContent(const EditorValue& value) {
    UiContentValue content;
    const EditorValue* fontAssetValue = field(value, "fontAsset");
    const EditorValue* horizontalValue = field(value, "horizontalAlign");
    const EditorValue* verticalValue = field(value, "verticalAlign");
    const EditorValue* textureAssetValue = field(value, "textureAsset");
    const EditorValue* fitValue = field(value, "imageFit");
    const EditorValue* clipValue = field(value, "clip");
    const auto* fontAsset = fontAssetValue ? fontAssetValue->getIf<std::string>() : nullptr;
    const auto* horizontal = horizontalValue ? horizontalValue->getIf<std::string>() : nullptr;
    const auto* vertical = verticalValue ? verticalValue->getIf<std::string>() : nullptr;
    const auto* textureAsset = textureAssetValue ? textureAssetValue->getIf<std::string>() : nullptr;
    const auto* fit = fitValue ? fitValue->getIf<std::string>() : nullptr;
    const auto* clip = clipValue ? clipValue->getIf<bool>() : nullptr;
    const double* fontSize = numberField(value, "fontSize");
    const double* textR = numberField(value, "textR");
    const double* textG = numberField(value, "textG");
    const double* textB = numberField(value, "textB");
    const double* textA = numberField(value, "textA");
    if (!fontAsset || !horizontal || !vertical || !textureAsset || !fit || !clip || !fontSize ||
        !textR || !textG || !textB || !textA)
        return serializationError<UiContentValue>(EditorStatus::Rejected,
            "editor.ui.invalid-content", "UI content fields are missing");
    content = {*fontAsset, *fontSize, *textR, *textG, *textB, *textA, *horizontal, *vertical,
               *textureAsset, *fit, *clip};
    const std::set<std::string> aligns{"start", "center", "end"};
    const std::set<std::string> fits{"stretch", "contain", "cover"};
    if (!std::isfinite(content.fontSize) || content.fontSize <= 0.0 || content.fontSize > 512.0 ||
        !std::isfinite(content.textR) || !std::isfinite(content.textG) ||
        !std::isfinite(content.textB) || !std::isfinite(content.textA) || content.textR < 0.0 ||
        content.textR > 1.0 || content.textG < 0.0 || content.textG > 1.0 ||
        content.textB < 0.0 || content.textB > 1.0 || content.textA < 0.0 ||
        content.textA > 1.0 || !aligns.contains(content.horizontalAlign) ||
        !aligns.contains(content.verticalAlign) || !fits.contains(content.imageFit))
        return serializationError<UiContentValue>(EditorStatus::Rejected,
            "editor.ui.invalid-content-range", "UI content size, color, alignment or image fit is invalid");
    return EditorResult<UiContentValue>::applied(std::move(content));
}

EditorValue UiDocumentTarget::widgetValue(const UiWidgetSnapshot& widget) {
    EditorValue::Object value;
    value["id"] = widget.id.value();
    value["parent"] = widget.parent.value();
    value["type"] = widget.type;
    value["name"] = widget.name;
    value["text"] = widget.text;
    value["visible"] = widget.visible;
    value["enabled"] = widget.enabled;
    value["layout"] = layoutValue(widget.layout);
    value["style"] = styleValue(widget.style);
    value["content"] = contentValue(widget.content);
    return EditorValue(std::move(value));
}

EditorResult<UiWidgetSnapshot> UiDocumentTarget::parseWidget(const EditorValue& value) {
    const EditorValue* idValue = field(value, "id");
    const EditorValue* parentValue = field(value, "parent");
    const EditorValue* typeValue = field(value, "type");
    const EditorValue* nameValue = field(value, "name");
    const EditorValue* textValue = field(value, "text");
    const EditorValue* visibleValue = field(value, "visible");
    const EditorValue* enabledValue = field(value, "enabled");
    const EditorValue* layout = field(value, "layout");
    const EditorValue* styleValueEntry = field(value, "style");
    const EditorValue* contentValueEntry = field(value, "content");
    const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto* parent = parentValue ? parentValue->getIf<std::string>() : nullptr;
    const auto* type = typeValue ? typeValue->getIf<std::string>() : nullptr;
    const auto* name = nameValue ? nameValue->getIf<std::string>() : nullptr;
    const auto* text = textValue ? textValue->getIf<std::string>() : nullptr;
    const auto* visible = visibleValue ? visibleValue->getIf<bool>() : nullptr;
    const auto* enabled = enabledValue ? enabledValue->getIf<bool>() : nullptr;
    if (!id || id->empty() || !parent || !type || type->empty() || !name || name->empty() ||
        !text || !visible || !enabled || !layout)
        return serializationError<UiWidgetSnapshot>(EditorStatus::Rejected, "editor.ui.invalid-widget",
                                                     "UI widget fields are missing or invalid");
    auto parsedLayout = parseLayout(*layout);
    if (!parsedLayout.accepted() || !parsedLayout.value)
        return serializationError<UiWidgetSnapshot>(EditorStatus::Rejected, "editor.ui.invalid-widget-layout",
                                                     "UI widget layout is invalid");
    UiStyleValue parsedStyle;
    if (styleValueEntry) {
        auto style = parseStyle(*styleValueEntry);
        if (!style.accepted() || !style.value)
            return serializationError<UiWidgetSnapshot>(EditorStatus::Rejected, "editor.ui.invalid-widget-style",
                                                         "UI widget style is invalid");
        parsedStyle = std::move(*style.value);
    }
    UiContentValue parsedContent;
    if (contentValueEntry) {
        auto content = parseContent(*contentValueEntry);
        if (!content.accepted() || !content.value)
            return serializationError<UiWidgetSnapshot>(EditorStatus::Rejected,
                "editor.ui.invalid-widget-content", "UI widget content skin is invalid");
        parsedContent = std::move(*content.value);
    }
    return EditorResult<UiWidgetSnapshot>::applied(
        {ObjectId(*id), ObjectId(*parent), *type, *name, *text, *visible, *enabled,
         *parsedLayout.value, std::move(parsedStyle), std::move(parsedContent)});
}

EditorValue UiDocumentTarget::snapshotValue() const {
    EditorValue::Array widgets;
    widgets.reserve(widgets_.size());
    for (const auto& [id, widget] : widgets_) {
        (void)id;
        widgets.push_back(widgetValue(widget));
    }
    EditorValue::Object root;
    root["schemaVersion"] = 2;
    root["widgets"] = EditorValue(std::move(widgets));
    return EditorValue(std::move(root));
}

EditorResult<void> UiDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = field(snapshot, "schemaVersion");
    const EditorValue* widgetsValue = field(snapshot, "widgets");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* widgets = widgetsValue ? widgetsValue->getIf<EditorValue::Array>() : nullptr;
    if (!version || (*version != 1 && *version != 2) || !widgets)
        return serializationError<void>(EditorStatus::Rejected, "editor.ui.snapshot-format",
                                        "UI snapshot requires schemaVersion 1 or 2 and a widget array");
    std::map<ObjectId, UiWidgetSnapshot> candidate;
    for (const EditorValue& value : *widgets) {
        auto parsed = parseWidget(value);
        if (!parsed.accepted() || !parsed.value)
            return serializationError<void>(EditorStatus::Rejected, "editor.ui.snapshot-widget",
                                            "UI snapshot contains an invalid widget");
        if (!candidate.emplace(parsed.value->id, *parsed.value).second)
            return serializationError<void>(EditorStatus::Conflict, "editor.ui.snapshot-duplicate",
                                            "UI snapshot contains duplicate widget ids");
    }
    for (const auto& [id, widget] : candidate) {
        if ((!widget.parent.empty() && !candidate.contains(widget.parent)) ||
            wouldCycle(id, widget.parent, candidate))
            return serializationError<void>(EditorStatus::Rejected, "editor.ui.snapshot-hierarchy",
                                            "UI snapshot hierarchy is missing a parent or contains a cycle");
    }
    widgets_ = std::move(candidate);
    ++revision_;
    dirty_.clear();
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
