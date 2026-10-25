#include "ui/editing/UiDocument.h"

#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <map>

namespace eve::ui_editing {
namespace {

Result<ui::NodeType> nodeType(const std::string& type) {
    static const std::map<std::string, ui::NodeType> types{{"progress", ui::NodeType::Progress},
                                                           {"filled-image", ui::NodeType::FilledImage},
                                                           {"radial-fill", ui::NodeType::RadialFill},
                                                           {"window", ui::NodeType::Window},
                                                           {"text", ui::NodeType::Text},
                                                           {"button", ui::NodeType::Button},
                                                           {"group", ui::NodeType::Group},
                                                           {"checkbox", ui::NodeType::Checkbox},
                                                           {"slider", ui::NodeType::Slider},
                                                           {"input", ui::NodeType::InputText},
                                                           {"flex", ui::NodeType::Flex},
                                                           {"image", ui::NodeType::Image},
                                                           {"image-button", ui::NodeType::ImageButton},
                                                           {"viewport", ui::NodeType::Viewport},
                                                           {"card", ui::NodeType::Card},
                                                           {"toolbar", ui::NodeType::Toolbar},
                                                           {"sidebar", ui::NodeType::Sidebar},
                                                           {"split-pane", ui::NodeType::SplitPane},
                                                           {"nine-patch", ui::NodeType::NinePatchPanel}};
    if (type == "row" || type == "column") return eve::editing::applied<ui::NodeType>(ui::NodeType::Flex);
    const auto found = types.find(type);
    if (found == types.end())
        return eve::editing::failed<ui::NodeType>(EditorStatus::Unsupported,
                                                  RuleId("editor.ui.unsupported-runtime-widget"),
                                                  "Runtime UI does not support widget type: " + type);
    return eve::editing::applied<ui::NodeType>(found->second);
}

Result<ui::WidgetDesc> buildWidget(const UiDocumentTarget& document, const ObjectId& id,
                                   const UiTextureResolver& textures, std::map<std::string, std::uint64_t>& cache) {
    auto value = document.widget(id);
    if (!value.ok())
        return eve::editing::failed<ui::WidgetDesc>(EditorStatus::NotFound, RuleId("editor.ui.widget-not-found"),
                                                    "UI widget does not exist: " + id.value());
    auto type = nodeType(value.value().type);
    if (!type.ok()) return Result<ui::WidgetDesc>::failure(type.status());
    const UiWidgetSnapshot& widget = value.value();
    if (!widget.content.fontAsset.empty() || widget.content.imageFit != "stretch")
        return eve::editing::failed<ui::WidgetDesc>(
            EditorStatus::Unsupported, RuleId("editor.ui.runtime-content-unsupported"),
            "Runtime document publishing requires the default font and stretch fit: " + id.value());
    ui::WidgetDesc result;
    result.type = type.value();
    result.withId(widget.id.value())
        .withKey(widget.id.value())
        .withText(widget.text)
        .withVisible(widget.visible)
        .withEnabled(widget.enabled)
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
        .withGap(static_cast<float>(widget.style.gap))
        .withFlexGrow(static_cast<float>(widget.style.flexGrow));
    if (!widget.content.textureAsset.empty()) {
        const bool supportsTexture = result.type == ui::NodeType::Image || result.type == ui::NodeType::ImageButton ||
                                     result.type == ui::NodeType::FilledImage ||
                                     result.type == ui::NodeType::NinePatchPanel;
        if (!supportsTexture || !textures)
            return eve::editing::failed<ui::WidgetDesc>(
                EditorStatus::Unsupported, RuleId("editor.ui.runtime-texture-resolver-required"),
                "Texture content requires a supported image widget and a texture resolver: " + id.value());
        auto found = cache.find(widget.content.textureAsset);
        if (found == cache.end()) {
            auto resolved = textures(widget.content.textureAsset);
            if (!resolved.ok()) return Result<ui::WidgetDesc>::failure(resolved.status());
            if (resolved.value() == 0)
                return eve::editing::failed<ui::WidgetDesc>(EditorStatus::Rejected,
                                                            RuleId("editor.ui.runtime-texture-invalid"),
                                                            "Texture resolver returned an empty registration");
            found = cache.emplace(widget.content.textureAsset, resolved.value()).first;
        }
        result.textureId = found->second;
    }
    result.value         = static_cast<float>(widget.content.value);
    result.fontSize      = static_cast<float>(widget.content.fontSize);
    result.textR         = static_cast<float>(widget.content.textR);
    result.textG         = static_cast<float>(widget.content.textG);
    result.textB         = static_cast<float>(widget.content.textB);
    result.textA         = static_cast<float>(widget.content.textA);
    const auto alignment = [](const std::string& value) {
        return value == "center" ? .5f : (value == "end" ? 1.f : 0.f);
    };
    result.textAlignX = alignment(widget.content.horizontalAlign);
    result.textAlignY = alignment(widget.content.verticalAlign);
    result.fillDirection =
        widget.content.fillDirection == "left"
            ? ui::FillDirection::Left
            : (widget.content.fillDirection == "down"
                   ? ui::FillDirection::Down
                   : (widget.content.fillDirection == "up" ? ui::FillDirection::Up : ui::FillDirection::Right));
    result.fillStretch      = widget.content.fillStretch;
    result.fillStartDegrees = static_cast<float>(widget.content.fillStartDegrees);
    result.fillClockwise    = widget.content.fillClockwise;
    result.fillInnerRadius  = static_cast<float>(widget.content.fillInnerRadius);
    if (result.type == ui::NodeType::FilledImage || result.type == ui::NodeType::RadialFill) {
        result.mouseFilter = ui::MouseFilter::Ignore;
        result.focusMode   = ui::FocusMode::None;
    }
    result.flexDirection = widget.type == "column" || (widget.type != "row" && widget.style.direction == "column")
                               ? ui::FlexDirection::Column
                               : ui::FlexDirection::Row;
    const std::map<std::string, ui::FlexAlign>   aligns{{"start", ui::FlexAlign::Start},
                                                        {"center", ui::FlexAlign::Center},
                                                        {"end", ui::FlexAlign::End},
                                                        {"stretch", ui::FlexAlign::Stretch}};
    const std::map<std::string, ui::FlexJustify> justifies{{"start", ui::FlexJustify::Start},
                                                           {"center", ui::FlexJustify::Center},
                                                           {"end", ui::FlexJustify::End},
                                                           {"space-between", ui::FlexJustify::SpaceBetween},
                                                           {"space-around", ui::FlexJustify::SpaceAround}};
    result.alignItems     = aligns.at(widget.style.align);
    result.justifyContent = justifies.at(widget.style.justify);
    for (const ObjectId& child : document.children(id)) {
        auto built = buildWidget(document, child, textures, cache);
        if (!built.ok()) return Result<ui::WidgetDesc>::failure(built.status());
        result.children.push_back(std::move(built).value());
    }
    return eve::editing::applied<ui::WidgetDesc>(std::move(result));
}

}  // namespace

Result<void> UiDocumentRuntimeBridge::publish(const UiDocumentTarget& document, const ObjectId& root, ui::UIHost* host,
                                              const UiTextureResolver& textures) const {
    if (!host)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.ui.runtime-host-required"),
                                          "Runtime UI publishing requires a live UIHost");
    std::map<std::string, std::uint64_t> cache;
    auto                                 built = buildWidget(document, root, textures, cache);
    if (!built.ok()) return Result<void>::failure(built.status());
    host->setTreeReconcile(std::move(built).value());
    return eve::editing::applied<void>();
}

}  // namespace eve::ui_editing
