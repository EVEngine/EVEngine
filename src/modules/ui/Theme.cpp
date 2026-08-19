#include "ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace eve::ui {
namespace {

Theme g_theme = Theme::dark();
std::string g_themeName = "dark";
float g_uiScale = 1.f;
float g_dpiScale = 1.f;

void copy4(float dst[4], const float src[4]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

void setCol(ImGuiStyle &style, ImGuiCol idx, const float c[4]) {
    style.Colors[idx] = ImVec4(c[0], c[1], c[2], c[3]);
}

std::string toLower(std::string s) {
    for (char &ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

}  // namespace

Theme Theme::dark() {
    Theme t;
    // Defaults in Theme already describe the dark preset.
    return t;
}

Theme Theme::light() {
    Theme t;
    // Shared geometry / typography with dark — only the palette changes.
    const float text[4] = {0.12f, 0.14f, 0.18f, 1.f};
    const float textDisabled[4] = {0.45f, 0.48f, 0.52f, 1.f};
    const float windowBg[4] = {0.94f, 0.95f, 0.97f, 1.f};
    const float childBg[4] = {0.90f, 0.92f, 0.95f, 1.f};
    const float popupBg[4] = {0.98f, 0.98f, 0.99f, 0.98f};
    const float border[4] = {0.55f, 0.58f, 0.62f, 0.55f};
    const float frameBg[4] = {0.86f, 0.88f, 0.92f, 1.f};
    const float frameBgHovered[4] = {0.80f, 0.84f, 0.90f, 1.f};
    const float frameBgActive[4] = {0.74f, 0.80f, 0.88f, 1.f};
    const float titleBg[4] = {0.88f, 0.90f, 0.94f, 1.f};
    const float titleBgActive[4] = {0.72f, 0.82f, 0.95f, 1.f};
    const float titleBgCollapsed[4] = {0.90f, 0.92f, 0.95f, 0.85f};
    const float button[4] = {0.26f, 0.52f, 0.90f, 0.65f};
    const float buttonHovered[4] = {0.28f, 0.55f, 0.95f, 1.f};
    const float buttonActive[4] = {0.18f, 0.42f, 0.82f, 1.f};
    const float header[4] = {0.26f, 0.52f, 0.90f, 0.35f};
    const float headerHovered[4] = {0.28f, 0.55f, 0.95f, 0.55f};
    const float headerActive[4] = {0.26f, 0.52f, 0.90f, 0.80f};
    const float checkMark[4] = {0.18f, 0.42f, 0.82f, 1.f};
    const float sliderGrab[4] = {0.30f, 0.55f, 0.92f, 0.90f};
    const float sliderGrabActive[4] = {0.22f, 0.48f, 0.88f, 1.f};
    const float separator[4] = {0.60f, 0.62f, 0.66f, 0.55f};
    const float scrollbarBg[4] = {0.85f, 0.87f, 0.90f, 0.60f};
    const float scrollbarGrab[4] = {0.55f, 0.58f, 0.62f, 0.80f};
    const float scrollbarGrabHovered[4] = {0.45f, 0.48f, 0.52f, 0.90f};
    const float scrollbarGrabActive[4] = {0.35f, 0.38f, 0.42f, 1.f};

    copy4(t.text, text);
    copy4(t.textDisabled, textDisabled);
    copy4(t.windowBg, windowBg);
    copy4(t.childBg, childBg);
    copy4(t.popupBg, popupBg);
    copy4(t.border, border);
    copy4(t.frameBg, frameBg);
    copy4(t.frameBgHovered, frameBgHovered);
    copy4(t.frameBgActive, frameBgActive);
    copy4(t.titleBg, titleBg);
    copy4(t.titleBgActive, titleBgActive);
    copy4(t.titleBgCollapsed, titleBgCollapsed);
    copy4(t.button, button);
    copy4(t.buttonHovered, buttonHovered);
    copy4(t.buttonActive, buttonActive);
    copy4(t.header, header);
    copy4(t.headerHovered, headerHovered);
    copy4(t.headerActive, headerActive);
    copy4(t.checkMark, checkMark);
    copy4(t.sliderGrab, sliderGrab);
    copy4(t.sliderGrabActive, sliderGrabActive);
    copy4(t.separator, separator);
    copy4(t.scrollbarBg, scrollbarBg);
    copy4(t.scrollbarGrab, scrollbarGrab);
    copy4(t.scrollbarGrabHovered, scrollbarGrabHovered);
    copy4(t.scrollbarGrabActive, scrollbarGrabActive);
    return t;
}

Theme &globalTheme() { return g_theme; }

const std::string &globalThemeName() { return g_themeName; }

void setGlobalTheme(const Theme &theme) { setGlobalTheme(theme, "custom"); }

void setGlobalTheme(const Theme &theme, const std::string &name) {
    g_theme = theme;
    g_themeName = name.empty() ? "custom" : name;
}

bool setThemeByName(const std::string &name) {
    const std::string key = toLower(name);
    if (key == "dark") {
        setGlobalTheme(Theme::dark(), "dark");
        return true;
    }
    if (key == "light") {
        setGlobalTheme(Theme::light(), "light");
        return true;
    }
    return false;
}

void setThemeUiScale(float scale) {
    g_uiScale = std::clamp(scale, 0.5f, 5.f);
}

float themeUiScale() { return g_uiScale; }

void setThemeDpiScale(float dpiScale) {
    g_dpiScale = std::clamp(dpiScale, 1.f, 5.f);
}

float themeDpiScale() { return g_dpiScale; }

void applyThemeToImGui(const Theme &theme) { applyThemeToImGui(theme, g_uiScale); }

void applyThemeToImGui(const Theme &theme, float uiScale) {
    ImGuiContext *ctx = ImGui::GetCurrentContext();
    if (!ctx) return;

    uiScale = std::clamp(uiScale, 0.5f, 5.f);
    const float s = uiScale;

    ImGuiStyle &style = ImGui::GetStyle();

    // Colors (independent of scale)
    setCol(style, ImGuiCol_Text, theme.text);
    setCol(style, ImGuiCol_TextDisabled, theme.textDisabled);
    setCol(style, ImGuiCol_WindowBg, theme.windowBg);
    setCol(style, ImGuiCol_ChildBg, theme.childBg);
    setCol(style, ImGuiCol_PopupBg, theme.popupBg);
    setCol(style, ImGuiCol_Border, theme.border);
    setCol(style, ImGuiCol_FrameBg, theme.frameBg);
    setCol(style, ImGuiCol_FrameBgHovered, theme.frameBgHovered);
    setCol(style, ImGuiCol_FrameBgActive, theme.frameBgActive);
    setCol(style, ImGuiCol_TitleBg, theme.titleBg);
    setCol(style, ImGuiCol_TitleBgActive, theme.titleBgActive);
    setCol(style, ImGuiCol_TitleBgCollapsed, theme.titleBgCollapsed);
    setCol(style, ImGuiCol_Button, theme.button);
    setCol(style, ImGuiCol_ButtonHovered, theme.buttonHovered);
    setCol(style, ImGuiCol_ButtonActive, theme.buttonActive);
    setCol(style, ImGuiCol_Header, theme.header);
    setCol(style, ImGuiCol_HeaderHovered, theme.headerHovered);
    setCol(style, ImGuiCol_HeaderActive, theme.headerActive);
    setCol(style, ImGuiCol_CheckMark, theme.checkMark);
    setCol(style, ImGuiCol_SliderGrab, theme.sliderGrab);
    setCol(style, ImGuiCol_SliderGrabActive, theme.sliderGrabActive);
    setCol(style, ImGuiCol_Separator, theme.separator);
    setCol(style, ImGuiCol_ScrollbarBg, theme.scrollbarBg);
    setCol(style, ImGuiCol_ScrollbarGrab, theme.scrollbarGrab);
    setCol(style, ImGuiCol_ScrollbarGrabHovered, theme.scrollbarGrabHovered);
    setCol(style, ImGuiCol_ScrollbarGrabActive, theme.scrollbarGrabActive);

    // Geometry + typography (scaled)
    style.WindowRounding = theme.windowRounding * s;
    style.ChildRounding = theme.childRounding * s;
    style.FrameRounding = theme.frameRounding * s;
    style.PopupRounding = theme.popupRounding * s;
    style.ScrollbarRounding = theme.scrollbarRounding * s;
    style.GrabRounding = theme.grabRounding * s;
    style.TabRounding = theme.tabRounding * s;

    style.WindowBorderSize = theme.windowBorderSize * s;
    style.ChildBorderSize = theme.childBorderSize * s;
    style.PopupBorderSize = theme.popupBorderSize * s;
    style.FrameBorderSize = theme.frameBorderSize * s;

    style.WindowPadding = ImVec2(theme.windowPaddingX * s, theme.windowPaddingY * s);
    style.FramePadding = ImVec2(theme.framePaddingX * s, theme.framePaddingY * s);
    style.ItemSpacing = ImVec2(theme.itemSpacingX * s, theme.itemSpacingY * s);
    style.ItemInnerSpacing = ImVec2(theme.itemInnerSpacingX * s, theme.itemInnerSpacingY * s);
    style.IndentSpacing = theme.indentSpacing * s;
    style.ScrollbarSize = theme.scrollbarSize * s;
    style.GrabMinSize = theme.grabMinSize * s;

    ImGuiIO &io = ImGui::GetIO();
    // The font atlas is rasterized at `baseSize * dpiScale` so glyphs stay
    // crisp on HiDPI displays, but ImGui already scales the whole frame by
    // io.DisplayFramebufferScale (which is the same DPI ratio). Cancelling it
    // here keeps the logical text size constant across display densities, so
    // the UI no longer double-scales (appears too large) on high-res screens.
    // FontGlobalScale carries the logical UI scale (uiScale) on top of the
    // user's per-theme font preference (theme.fontScale).
    const float dpi = std::clamp(g_dpiScale, 1.f, 5.f);
    io.FontGlobalScale = theme.fontScale * (s / dpi);

    if (theme.navEnableKeyboard) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    if (theme.navEnableGamepad) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}

}  // namespace eve::ui
