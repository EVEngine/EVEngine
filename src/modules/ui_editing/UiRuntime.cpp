#include "ui_editing/UiDocument.h"

#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <map>

namespace eve::ui_editing {
namespace {

EditorResult<ui::NodeType> nodeType(const std::string& type) {
    static const std::map<std::string, ui::NodeType> types{
        {"window", ui::NodeType::Window}, {"text", ui::NodeType::Text},
        {"button", ui::NodeType::Button}, {"group", ui::NodeType::Group},
        {"checkbox", ui::NodeType::Checkbox}, {"slider", ui::NodeType::Slider},
        {"input", ui::NodeType::InputText}, {"flex", ui::NodeType::Flex},
        {"image", ui::NodeType::Image}, {"image-button", ui::NodeType::ImageButton},
        {"viewport", ui::NodeType::Viewport}, {"card", ui::NodeType::Card},
        {"toolbar", ui::NodeType::Toolbar}, {"sidebar", ui::NodeType::Sidebar},
        {"split-pane", ui::NodeType::SplitPane}, {"nine-patch", ui::NodeType::NinePatchPanel}};
    const auto found = types.find(type);
    if (found == types.end())
        return EditorResult<ui::NodeType>::error(EditorStatus::Unsupported,
            RuleId("editor.ui.unsupported-runtime-widget"), "Runtime UI does not support widget type: " + type);
    return EditorResult<ui::NodeType>::applied(found->second);
}

EditorResult<ui::WidgetDesc> buildWidget(const UiDocumentTarget& document, const ObjectId& id) {
    auto value = document.widget(id);
    if (!value.value)
        return EditorResult<ui::WidgetDesc>::error(EditorStatus::NotFound,
            RuleId("editor.ui.widget-not-found"), "UI widget does not exist: " + id.value());
    auto type = nodeType(value.value->type);
    if (!type.value)
        return EditorResult<ui::WidgetDesc>::error(type.status, type.diagnostics.front().rule,
                                                   type.diagnostics.front().message);
    const UiWidgetSnapshot& widget = *value.value;
    ui::WidgetDesc result;
    result.type = *type.value;
    result.withId(widget.id.value()).withKey(widget.id.value()).withText(widget.text)
        .withVisible(widget.visible).withEnabled(widget.enabled)
        .withSize(static_cast<float>(widget.layout.width), static_cast<float>(widget.layout.height))
        .withAbsolute(static_cast<float>(widget.layout.anchorX), static_cast<float>(widget.layout.anchorY),
                      static_cast<float>(widget.layout.x - widget.layout.width * widget.layout.pivotX),
                      static_cast<float>(widget.layout.y - widget.layout.height * widget.layout.pivotY))
        .withMargin(static_cast<float>(widget.style.marginLeft), static_cast<float>(widget.style.marginTop),
                    static_cast<float>(widget.style.marginRight), static_cast<float>(widget.style.marginBottom))
        .withPadding(static_cast<float>(widget.style.paddingLeft), static_cast<float>(widget.style.paddingTop),
                     static_cast<float>(widget.style.paddingRight), static_cast<float>(widget.style.paddingBottom))
        .withTint(static_cast<float>(widget.style.tintR), static_cast<float>(widget.style.tintG),
                  static_cast<float>(widget.style.tintB), static_cast<float>(widget.style.tintA))
        .withCornerRadius(static_cast<float>(widget.style.cornerRadius))
        .withGap(static_cast<float>(widget.style.gap)).withFlexGrow(static_cast<float>(widget.style.flexGrow));
    result.flexDirection = widget.style.direction == "column" ? ui::FlexDirection::Column : ui::FlexDirection::Row;
    const std::map<std::string, ui::FlexAlign> aligns{{"start", ui::FlexAlign::Start},
        {"center", ui::FlexAlign::Center}, {"end", ui::FlexAlign::End}, {"stretch", ui::FlexAlign::Stretch}};
    const std::map<std::string, ui::FlexJustify> justifies{{"start", ui::FlexJustify::Start},
        {"center", ui::FlexJustify::Center}, {"end", ui::FlexJustify::End},
        {"space-between", ui::FlexJustify::SpaceBetween}, {"space-around", ui::FlexJustify::SpaceAround}};
    result.alignItems = aligns.at(widget.style.align);
    result.justifyContent = justifies.at(widget.style.justify);
    for (const ObjectId& child : document.children(id)) {
        auto built = buildWidget(document, child);
        if (!built.value)
            return EditorResult<ui::WidgetDesc>::error(built.status, built.diagnostics.front().rule,
                                                        built.diagnostics.front().message);
        result.children.push_back(std::move(*built.value));
    }
    return EditorResult<ui::WidgetDesc>::applied(std::move(result));
}

}  // namespace

EditorResult<void> UiDocumentRuntimeBridge::publish(const UiDocumentTarget& document,
                                                     const ObjectId& root, ui::UIHost* host) const {
    if (!host)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.ui.runtime-host-required"),
                                         "Runtime UI publishing requires a live UIHost");
    auto built = buildWidget(document, root);
    if (!built.value)
        return EditorResult<void>::error(built.status, built.diagnostics.front().rule,
                                         built.diagnostics.front().message);
    host->setTreeReconcile(std::move(*built.value));
    return EditorResult<void>::applied();
}

}  // namespace eve::ui_editing
