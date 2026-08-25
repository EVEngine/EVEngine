#pragma once

#include <string>

namespace eve::ui {

/**
 * @brief Semantic layout tokens shared by editor-oriented widgets.
 *
 * Values are logical design units at uiScale=1. Components use these values
 * only when the corresponding WidgetDesc property is unset, so per-widget
 * size, padding and margin remain the higher-specificity override.
 */
struct ThemeLayout {
    float toolbarHeight = 48.f;
    float statusBarHeight = 36.f;
    float sidebarWidth = 240.f;
    float toolboxCellSize = 44.f;
    float splitterSize = 6.f;
    float minPaneSize = 160.f;
    float panelPaddingX = 12.f;
    float panelPaddingY = 10.f;
    float cardPaddingX = 14.f;
    float cardPaddingY = 12.f;
    float barPaddingX = 10.f;
    float barPaddingY = 6.f;
    float sectionSpacingY = 6.f;
    float searchMinWidth = 120.f;
    float searchIconGap = 7.f;
};

/**
 * @brief Unified UI design tokens (colors, geometry, typography).
 * Applied to ImGui each frame via applyThemeToImGui; metrics are design-time
 * units at uiScale=1 and are multiplied by the current UI scale.
 */
struct Theme {
    /** CSS-like component layout defaults; explicit widget metrics override them. */
    ThemeLayout layout;

    // --- Colors (RGBA 0..1) ---
    float text[4] = {0.87f, 0.89f, 0.93f, 1.f};
    float textDisabled[4] = {0.55f, 0.58f, 0.65f, 1.f};
    float windowBg[4] = {0.145f, 0.155f, 0.19f, 0.99f};
    float childBg[4] = {0.18f, 0.195f, 0.24f, 0.98f};
    float popupBg[4] = {0.18f, 0.195f, 0.24f, 0.99f};
    float border[4] = {0.095f, 0.105f, 0.135f, 0.92f};
    float borderShadow[4] = {0.f, 0.f, 0.f, 0.f};
    float frameBg[4] = {0.16f, 0.17f, 0.21f, 1.f};
    float frameBgHovered[4] = {0.21f, 0.225f, 0.275f, 1.f};
    float frameBgActive[4] = {0.25f, 0.22f, 0.26f, 1.f};
    float titleBg[4] = {0.21f, 0.225f, 0.275f, 1.f};
    float titleBgActive[4] = {0.235f, 0.25f, 0.305f, 1.f};
    float titleBgCollapsed[4] = {0.19f, 0.205f, 0.25f, 0.96f};
    float menuBarBg[4] = {0.225f, 0.24f, 0.29f, 1.f};
    float button[4] = {0.18f, 0.195f, 0.24f, 1.f};
    float buttonHovered[4] = {0.43f, 0.25f, 0.30f, 1.f};
    float buttonActive[4] = {0.98f, 0.36f, 0.43f, 1.f};
    float header[4] = {0.25f, 0.22f, 0.27f, 0.78f};
    float headerHovered[4] = {0.43f, 0.25f, 0.30f, 0.92f};
    float headerActive[4] = {0.60f, 0.29f, 0.34f, 1.f};
    float checkMark[4] = {1.f, 0.39f, 0.45f, 1.f};
    float sliderGrab[4] = {0.92f, 0.34f, 0.40f, 0.94f};
    float sliderGrabActive[4] = {1.f, 0.46f, 0.51f, 1.f};
    float separator[4] = {0.095f, 0.105f, 0.135f, 0.88f};
    float scrollbarBg[4] = {0.105f, 0.115f, 0.15f, 0.62f};
    float scrollbarGrab[4] = {0.29f, 0.31f, 0.37f, 0.90f};
    float scrollbarGrabHovered[4] = {0.39f, 0.41f, 0.48f, 0.96f};
    float scrollbarGrabActive[4] = {0.55f, 0.31f, 0.36f, 1.f};
    float tab[4] = {0.18f, 0.195f, 0.24f, 1.f};
    float tabHovered[4] = {0.42f, 0.25f, 0.30f, 1.f};
    float tabActive[4] = {0.28f, 0.225f, 0.27f, 1.f};
    float tabUnfocused[4] = {0.155f, 0.165f, 0.205f, 1.f};
    float tabUnfocusedActive[4] = {0.22f, 0.20f, 0.24f, 1.f};
    float tableHeaderBg[4] = {0.20f, 0.215f, 0.26f, 1.f};
    float tableBorderStrong[4] = {0.095f, 0.105f, 0.135f, 0.92f};
    float tableBorderLight[4] = {0.14f, 0.15f, 0.19f, 0.72f};
    float tableRowBg[4] = {0.f, 0.f, 0.f, 0.f};
    float tableRowBgAlt[4] = {0.31f, 0.33f, 0.39f, 0.22f};
    float plotLines[4] = {0.62f, 0.65f, 0.72f, 1.f};
    float plotLinesHovered[4] = {1.f, 0.39f, 0.45f, 1.f};
    float plotHistogram[4] = {0.85f, 0.60f, 0.28f, 1.f};
    float plotHistogramHovered[4] = {0.96f, 0.70f, 0.34f, 1.f};
    float textSelectedBg[4] = {0.90f, 0.31f, 0.38f, 0.42f};
    float navHighlight[4] = {1.f, 0.39f, 0.45f, 0.88f};
    float modalDimBg[4] = {0.055f, 0.06f, 0.08f, 0.72f};

    // --- Geometry (design units @ scale 1) ---
    float windowRounding = 3.f;
    float childRounding = 3.f;
    float frameRounding = 3.f;
    float popupRounding = 4.f;
    float scrollbarRounding = 3.f;
    float grabRounding = 3.f;
    float tabRounding = 2.f;

    float windowBorderSize = 1.f;
    float childBorderSize = 1.f;
    float popupBorderSize = 1.f;
    float frameBorderSize = 0.f;

    float windowPaddingX = 14.f;
    float windowPaddingY = 12.f;
    float framePaddingX = 9.f;
    float framePaddingY = 5.f;
    float itemSpacingX = 9.f;
    float itemSpacingY = 7.f;
    float itemInnerSpacingX = 6.f;
    float itemInnerSpacingY = 4.f;
    float cellPaddingX = 6.f;
    float cellPaddingY = 3.f;
    float indentSpacing = 16.f;
    float scrollbarSize = 10.f;
    float grabMinSize = 8.f;

    /** Font size multiplier relative to the base UI font (FontGlobalScale).
     *  Display DPI is cancelled out of FontGlobalScale so this stays logical. */
    float fontScale = 1.f;
    bool navEnableKeyboard = true;
    bool navEnableGamepad = false;

    /** @brief Built-in presets with matching geometry; only colors differ. */
    static Theme dark();
    static Theme light();
};

Theme &globalTheme();
/** @brief Current preset name: "dark", "light", or "custom". */
const std::string &globalThemeName();

void setGlobalTheme(const Theme &theme);
void setGlobalTheme(const Theme &theme, const std::string &name);

/** @brief Apply a named preset ("dark" / "light"). Case-insensitive. Returns false if unknown. */
bool setThemeByName(const std::string &name);

/** @brief Logical (point-space) UI scale. Default 1.0. */
void setThemeUiScale(float scale);
float themeUiScale();

/** Display DPI ratio (e.g. 1.5 on Windows at 150%, 2.0 on Retina). The font
 *  atlas is rasterized at this resolution so glyphs stay crisp; UI scale and
 *  FontGlobalScale then preserve the intended logical size per platform. */
void setThemeDpiScale(float dpiScale);
float themeDpiScale();

/** @brief Push tokens into ImGui style. Metrics are multiplied by uiScale (default: themeUiScale()). */
void applyThemeToImGui(const Theme &theme);
void applyThemeToImGui(const Theme &theme, float uiScale);

}  // namespace eve::ui
