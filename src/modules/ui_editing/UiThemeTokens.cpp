#include "ui_editing/UiThemeTokens.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace eve::ui_editing {
namespace {

template <class T>
EditorResult<T> tokenError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

EditorDiagnostic tokenDiagnostic(const char* rule, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId(rule),
                                        DiagnosticSeverity::Error, std::move(message));
}

struct ColorTok {
    const char* path;
    float (ui::Theme::*member)[4];
};

struct FloatTok {
    const char* path;
    float ui::Theme::* member;
    float              minimum;
    float              maximum;
    const char*        category;
};

struct LayoutTok {
    const char* path;
    float ui::ThemeLayout::* member;
    float                    minimum;
    float                    maximum;
};

const ColorTok kColors[] = {
    {"color.text", &ui::Theme::text},
    {"color.textDisabled", &ui::Theme::textDisabled},
    {"color.windowBg", &ui::Theme::windowBg},
    {"color.childBg", &ui::Theme::childBg},
    {"color.popupBg", &ui::Theme::popupBg},
    {"color.border", &ui::Theme::border},
    {"color.borderShadow", &ui::Theme::borderShadow},
    {"color.frameBg", &ui::Theme::frameBg},
    {"color.frameBgHovered", &ui::Theme::frameBgHovered},
    {"color.frameBgActive", &ui::Theme::frameBgActive},
    {"color.titleBg", &ui::Theme::titleBg},
    {"color.titleBgActive", &ui::Theme::titleBgActive},
    {"color.titleBgCollapsed", &ui::Theme::titleBgCollapsed},
    {"color.menuBarBg", &ui::Theme::menuBarBg},
    {"color.button", &ui::Theme::button},
    {"color.buttonHovered", &ui::Theme::buttonHovered},
    {"color.buttonActive", &ui::Theme::buttonActive},
    {"color.header", &ui::Theme::header},
    {"color.headerHovered", &ui::Theme::headerHovered},
    {"color.headerActive", &ui::Theme::headerActive},
    {"color.checkMark", &ui::Theme::checkMark},
    {"color.sliderGrab", &ui::Theme::sliderGrab},
    {"color.sliderGrabActive", &ui::Theme::sliderGrabActive},
    {"color.separator", &ui::Theme::separator},
    {"color.scrollbarBg", &ui::Theme::scrollbarBg},
    {"color.scrollbarGrab", &ui::Theme::scrollbarGrab},
    {"color.scrollbarGrabHovered", &ui::Theme::scrollbarGrabHovered},
    {"color.scrollbarGrabActive", &ui::Theme::scrollbarGrabActive},
    {"color.tab", &ui::Theme::tab},
    {"color.tabHovered", &ui::Theme::tabHovered},
    {"color.tabActive", &ui::Theme::tabActive},
    {"color.tabUnfocused", &ui::Theme::tabUnfocused},
    {"color.tabUnfocusedActive", &ui::Theme::tabUnfocusedActive},
    {"color.tableHeaderBg", &ui::Theme::tableHeaderBg},
    {"color.tableBorderStrong", &ui::Theme::tableBorderStrong},
    {"color.tableBorderLight", &ui::Theme::tableBorderLight},
    {"color.tableRowBg", &ui::Theme::tableRowBg},
    {"color.tableRowBgAlt", &ui::Theme::tableRowBgAlt},
    {"color.plotLines", &ui::Theme::plotLines},
    {"color.plotLinesHovered", &ui::Theme::plotLinesHovered},
    {"color.plotHistogram", &ui::Theme::plotHistogram},
    {"color.plotHistogramHovered", &ui::Theme::plotHistogramHovered},
    {"color.textSelectedBg", &ui::Theme::textSelectedBg},
    {"color.navHighlight", &ui::Theme::navHighlight},
    {"color.modalDimBg", &ui::Theme::modalDimBg},
};

const FloatTok kFloats[] = {
    {"geometry.windowRounding", &ui::Theme::windowRounding, 0.f, 24.f, "Geometry"},
    {"geometry.childRounding", &ui::Theme::childRounding, 0.f, 24.f, "Geometry"},
    {"geometry.frameRounding", &ui::Theme::frameRounding, 0.f, 24.f, "Geometry"},
    {"geometry.popupRounding", &ui::Theme::popupRounding, 0.f, 24.f, "Geometry"},
    {"geometry.scrollbarRounding", &ui::Theme::scrollbarRounding, 0.f, 24.f, "Geometry"},
    {"geometry.grabRounding", &ui::Theme::grabRounding, 0.f, 24.f, "Geometry"},
    {"geometry.tabRounding", &ui::Theme::tabRounding, 0.f, 24.f, "Geometry"},
    {"geometry.windowBorderSize", &ui::Theme::windowBorderSize, 0.f, 8.f, "Geometry"},
    {"geometry.childBorderSize", &ui::Theme::childBorderSize, 0.f, 8.f, "Geometry"},
    {"geometry.popupBorderSize", &ui::Theme::popupBorderSize, 0.f, 8.f, "Geometry"},
    {"geometry.frameBorderSize", &ui::Theme::frameBorderSize, 0.f, 8.f, "Geometry"},
    {"spacing.windowPaddingX", &ui::Theme::windowPaddingX, 0.f, 64.f, "Spacing"},
    {"spacing.windowPaddingY", &ui::Theme::windowPaddingY, 0.f, 64.f, "Spacing"},
    {"spacing.framePaddingX", &ui::Theme::framePaddingX, 0.f, 64.f, "Spacing"},
    {"spacing.framePaddingY", &ui::Theme::framePaddingY, 0.f, 64.f, "Spacing"},
    {"spacing.itemSpacingX", &ui::Theme::itemSpacingX, 0.f, 64.f, "Spacing"},
    {"spacing.itemSpacingY", &ui::Theme::itemSpacingY, 0.f, 64.f, "Spacing"},
    {"spacing.itemInnerSpacingX", &ui::Theme::itemInnerSpacingX, 0.f, 64.f, "Spacing"},
    {"spacing.itemInnerSpacingY", &ui::Theme::itemInnerSpacingY, 0.f, 64.f, "Spacing"},
    {"spacing.cellPaddingX", &ui::Theme::cellPaddingX, 0.f, 64.f, "Spacing"},
    {"spacing.cellPaddingY", &ui::Theme::cellPaddingY, 0.f, 64.f, "Spacing"},
    {"spacing.indentSpacing", &ui::Theme::indentSpacing, 0.f, 64.f, "Spacing"},
    {"spacing.scrollbarSize", &ui::Theme::scrollbarSize, 1.f, 64.f, "Spacing"},
    {"spacing.grabMinSize", &ui::Theme::grabMinSize, 1.f, 64.f, "Spacing"},
    {"typography.fontScale", &ui::Theme::fontScale, 0.25f, 4.f, "Typography"},
};

const LayoutTok kLayout[] = {
    {"layout.toolbarHeight", &ui::ThemeLayout::toolbarHeight, 16.f, 128.f},
    {"layout.statusBarHeight", &ui::ThemeLayout::statusBarHeight, 16.f, 128.f},
    {"layout.sidebarWidth", &ui::ThemeLayout::sidebarWidth, 80.f, 640.f},
    {"layout.toolboxCellSize", &ui::ThemeLayout::toolboxCellSize, 16.f, 128.f},
    {"layout.splitterSize", &ui::ThemeLayout::splitterSize, 2.f, 24.f},
    {"layout.minPaneSize", &ui::ThemeLayout::minPaneSize, 32.f, 640.f},
    {"layout.panelPaddingX", &ui::ThemeLayout::panelPaddingX, 0.f, 64.f},
    {"layout.panelPaddingY", &ui::ThemeLayout::panelPaddingY, 0.f, 64.f},
    {"layout.cardPaddingX", &ui::ThemeLayout::cardPaddingX, 0.f, 64.f},
    {"layout.cardPaddingY", &ui::ThemeLayout::cardPaddingY, 0.f, 64.f},
    {"layout.barPaddingX", &ui::ThemeLayout::barPaddingX, 0.f, 64.f},
    {"layout.barPaddingY", &ui::ThemeLayout::barPaddingY, 0.f, 64.f},
    {"layout.sectionSpacingY", &ui::ThemeLayout::sectionSpacingY, 0.f, 64.f},
    {"layout.searchMinWidth", &ui::ThemeLayout::searchMinWidth, 32.f, 640.f},
    {"layout.searchIconGap", &ui::ThemeLayout::searchIconGap, 0.f, 32.f},
};

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool readColor(const EditorValue& value, float dest[4]) {
    const auto* array = value.getIf<EditorValue::Array>();
    if (!array || array->size() < 4) return false;
    for (int i = 0; i < 4; ++i) {
        const auto* number = (*array)[static_cast<std::size_t>(i)].getIf<double>();
        if (!number || !std::isfinite(*number) || *number < 0.0 || *number > 1.0) return false;
        dest[i] = static_cast<float>(*number);
    }
    return true;
}

EditorValue colorValue(const float src[4]) {
    return EditorValue::Array{static_cast<double>(src[0]), static_cast<double>(src[1]), static_cast<double>(src[2]),
                              static_cast<double>(src[3])};
}

PropertyDescriptor colorProperty(const char* path) {
    PropertyDescriptor descriptor;
    descriptor.path           = PropertyPath(path);
    descriptor.displayNameKey = path;
    descriptor.category       = "Colors";
    descriptor.type           = PropertyType::Color;
    descriptor.flags          = PropertyFlag::Runtime;
    descriptor.numeric.minimum = 0.0;
    descriptor.numeric.maximum = 1.0;
    descriptor.defaultValue   = EditorValue::Array{1.0, 1.0, 1.0, 1.0};
    return descriptor;
}

PropertyDescriptor floatProperty(const FloatTok& token) {
    PropertyDescriptor descriptor;
    descriptor.path            = PropertyPath(token.path);
    descriptor.displayNameKey  = token.path;
    descriptor.category        = token.category;
    descriptor.type            = PropertyType::Float;
    descriptor.flags           = PropertyFlag::Runtime;
    descriptor.numeric.minimum = token.minimum;
    descriptor.numeric.maximum = token.maximum;
    descriptor.defaultValue    = static_cast<double>(token.minimum);
    return descriptor;
}

PropertyDescriptor layoutProperty(const LayoutTok& token) {
    PropertyDescriptor descriptor;
    descriptor.path            = PropertyPath(token.path);
    descriptor.displayNameKey  = token.path;
    descriptor.category        = "Layout";
    descriptor.type            = PropertyType::Float;
    descriptor.flags           = PropertyFlag::Runtime;
    descriptor.numeric.minimum = token.minimum;
    descriptor.numeric.maximum = token.maximum;
    descriptor.defaultValue    = static_cast<double>(token.minimum);
    return descriptor;
}

bool inRange(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

}  // namespace

EditorValue themeTokensValue(const ui::Theme& theme) {
    EditorValue::Object object;
    for (const ColorTok& token : kColors) object[token.path] = colorValue(theme.*(token.member));
    for (const FloatTok& token : kFloats)
        object[token.path] = static_cast<double>(theme.*(token.member));
    for (const LayoutTok& token : kLayout)
        object[token.path] = static_cast<double>(theme.layout.*(token.member));
    object["nav.keyboard"] = theme.navEnableKeyboard;
    object["nav.gamepad"]  = theme.navEnableGamepad;
    return EditorValue(std::move(object));
}

EditorResult<ui::Theme> parseThemeTokens(const EditorValue& value) {
    if (!value.getIf<EditorValue::Object>())
        return tokenError<ui::Theme>(EditorStatus::Rejected, "editor.ui-theme.tokens",
                                     "Theme tokens must be an object");
    ui::Theme theme;
    for (const ColorTok& token : kColors) {
        const EditorValue* entry = field(value, token.path);
        if (!entry || !readColor(*entry, theme.*(token.member)))
            return tokenError<ui::Theme>(EditorStatus::Rejected, "editor.ui-theme.color",
                                         std::string("Theme color is missing or invalid: ") + token.path);
    }
    for (const FloatTok& token : kFloats) {
        const EditorValue* entry  = field(value, token.path);
        const auto*        number = entry ? entry->getIf<double>() : nullptr;
        if (!number || !std::isfinite(*number))
            return tokenError<ui::Theme>(EditorStatus::Rejected, "editor.ui-theme.float",
                                         std::string("Theme number is missing or invalid: ") + token.path);
        theme.*(token.member) = static_cast<float>(*number);
    }
    for (const LayoutTok& token : kLayout) {
        const EditorValue* entry  = field(value, token.path);
        const auto*        number = entry ? entry->getIf<double>() : nullptr;
        if (!number || !std::isfinite(*number))
            return tokenError<ui::Theme>(EditorStatus::Rejected, "editor.ui-theme.layout",
                                         std::string("Theme layout number is missing or invalid: ") + token.path);
        theme.layout.*(token.member) = static_cast<float>(*number);
    }
    const EditorValue* keyboard = field(value, "nav.keyboard");
    const EditorValue* gamepad  = field(value, "nav.gamepad");
    const auto* keyboardFlag = keyboard ? keyboard->getIf<bool>() : nullptr;
    const auto* gamepadFlag  = gamepad ? gamepad->getIf<bool>() : nullptr;
    if (!keyboardFlag || !gamepadFlag)
        return tokenError<ui::Theme>(EditorStatus::Rejected, "editor.ui-theme.nav",
                                     "Theme navigation flags are required");
    theme.navEnableKeyboard = *keyboardFlag;
    theme.navEnableGamepad  = *gamepadFlag;
    auto diagnostics = validateThemeTokens(theme);
    if (!diagnostics.empty())
        return tokenError<ui::Theme>(EditorStatus::Rejected, "editor.ui-theme.range",
                                     diagnostics.front().message());
    return eve::editing::applied<ui::Theme>(theme);
}

EditorResult<void> assignThemeToken(ui::Theme& theme, const PropertyPath& path, const EditorValue& value) {
    for (const ColorTok& token : kColors) {
        if (path != PropertyPath(token.path)) continue;
        if (!readColor(value, theme.*(token.member)))
            return tokenError<void>(EditorStatus::Rejected, "editor.ui-theme.token-color",
                                    std::string("Theme color token is invalid: ") + token.path);
        return eve::editing::applied<void>();
    }
    for (const FloatTok& token : kFloats) {
        if (path != PropertyPath(token.path)) continue;
        const auto* number = value.getIf<double>();
        if (!number || !inRange(static_cast<float>(*number), token.minimum, token.maximum))
            return tokenError<void>(EditorStatus::Rejected, "editor.ui-theme.token-float",
                                    std::string("Theme number token is invalid: ") + token.path);
        theme.*(token.member) = static_cast<float>(*number);
        return eve::editing::applied<void>();
    }
    for (const LayoutTok& token : kLayout) {
        if (path != PropertyPath(token.path)) continue;
        const auto* number = value.getIf<double>();
        if (!number || !inRange(static_cast<float>(*number), token.minimum, token.maximum))
            return tokenError<void>(EditorStatus::Rejected, "editor.ui-theme.token-layout",
                                    std::string("Theme layout token is invalid: ") + token.path);
        theme.layout.*(token.member) = static_cast<float>(*number);
        return eve::editing::applied<void>();
    }
    if (path == PropertyPath("nav.keyboard")) {
        const auto* flag = value.getIf<bool>();
        if (!flag)
            return tokenError<void>(EditorStatus::Rejected, "editor.ui-theme.token-nav",
                                    "Theme navigation token is invalid");
        theme.navEnableKeyboard = *flag;
        return eve::editing::applied<void>();
    }
    if (path == PropertyPath("nav.gamepad")) {
        const auto* flag = value.getIf<bool>();
        if (!flag)
            return tokenError<void>(EditorStatus::Rejected, "editor.ui-theme.token-nav",
                                    "Theme navigation token is invalid");
        theme.navEnableGamepad = *flag;
        return eve::editing::applied<void>();
    }
    return tokenError<void>(EditorStatus::NotFound, "editor.ui-theme.token",
                            "Unknown theme token: " + path.value());
}

EditorValue readThemeToken(const ui::Theme& theme, const PropertyPath& path) {
    for (const ColorTok& token : kColors) {
        if (path == PropertyPath(token.path)) return colorValue(theme.*(token.member));
    }
    for (const FloatTok& token : kFloats) {
        if (path == PropertyPath(token.path)) return static_cast<double>(theme.*(token.member));
    }
    for (const LayoutTok& token : kLayout) {
        if (path == PropertyPath(token.path)) return static_cast<double>(theme.layout.*(token.member));
    }
    if (path == PropertyPath("nav.keyboard")) return theme.navEnableKeyboard;
    if (path == PropertyPath("nav.gamepad")) return theme.navEnableGamepad;
    return {};
}

PropertySchema themeTokenSchema() {
    PropertySchema schema;
    schema.typeId = "ui.theme";
    schema.properties.push_back(colorProperty("color.text"));
    schema.properties.push_back(colorProperty("color.windowBg"));
    schema.properties.push_back(colorProperty("color.childBg"));
    schema.properties.push_back(colorProperty("color.frameBg"));
    schema.properties.push_back(colorProperty("color.button"));
    schema.properties.push_back(colorProperty("color.buttonHovered"));
    schema.properties.push_back(colorProperty("color.buttonActive"));
    schema.properties.push_back(colorProperty("color.header"));
    schema.properties.push_back(colorProperty("color.checkMark"));
    schema.properties.push_back(colorProperty("color.sliderGrab"));
    for (const FloatTok& token : kFloats) schema.properties.push_back(floatProperty(token));
    for (const LayoutTok& token : kLayout) schema.properties.push_back(layoutProperty(token));
    PropertyDescriptor keyboard;
    keyboard.path           = PropertyPath("nav.keyboard");
    keyboard.displayNameKey = "nav.keyboard";
    keyboard.category       = "Typography";
    keyboard.type           = PropertyType::Bool;
    keyboard.flags          = PropertyFlag::Runtime;
    keyboard.defaultValue   = true;
    schema.properties.push_back(keyboard);
    PropertyDescriptor gamepad = keyboard;
    gamepad.path               = PropertyPath("nav.gamepad");
    gamepad.displayNameKey     = "nav.gamepad";
    gamepad.defaultValue       = false;
    schema.properties.push_back(std::move(gamepad));
    return schema;
}

std::vector<EditorDiagnostic> validateThemeTokens(const ui::Theme& theme) {
    std::vector<EditorDiagnostic> diagnostics;
    for (const ColorTok& token : kColors) {
        const float* color = theme.*(token.member);
        for (int i = 0; i < 4; ++i) {
            if (!inRange(color[i], 0.f, 1.f)) {
                diagnostics.push_back(tokenDiagnostic("editor.ui-theme.color-range",
                                                      std::string("Theme color channel is out of range: ") + token.path));
                break;
            }
        }
    }
    for (const FloatTok& token : kFloats) {
        if (!inRange(theme.*(token.member), token.minimum, token.maximum))
            diagnostics.push_back(tokenDiagnostic("editor.ui-theme.float-range",
                                                  std::string("Theme number is out of range: ") + token.path));
    }
    for (const LayoutTok& token : kLayout) {
        if (!inRange(theme.layout.*(token.member), token.minimum, token.maximum))
            diagnostics.push_back(tokenDiagnostic("editor.ui-theme.layout-range",
                                                  std::string("Theme layout number is out of range: ") + token.path));
    }
    return diagnostics;
}

ui::Theme themeFromPreset(UiThemeBasePreset preset) {
    return preset == UiThemeBasePreset::Light ? ui::Theme::light() : ui::Theme::dark();
}

std::string presetName(UiThemeBasePreset preset) {
    switch (preset) {
        case UiThemeBasePreset::Light: return "light";
        case UiThemeBasePreset::Custom: return "custom";
        case UiThemeBasePreset::Dark: return "dark";
    }
    return "dark";
}

EditorResult<UiThemeBasePreset> parsePreset(const std::string& name) {
    if (name == "dark") return eve::editing::applied<UiThemeBasePreset>(UiThemeBasePreset::Dark);
    if (name == "light") return eve::editing::applied<UiThemeBasePreset>(UiThemeBasePreset::Light);
    if (name == "custom") return eve::editing::applied<UiThemeBasePreset>(UiThemeBasePreset::Custom);
    return tokenError<UiThemeBasePreset>(EditorStatus::Rejected, "editor.ui-theme.preset",
                                         "Theme base preset must be dark, light, or custom");
}

}  // namespace eve::ui_editing
