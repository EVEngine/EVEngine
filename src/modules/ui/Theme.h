#pragma once

#include <string>

namespace eve::ui {

/**
 * Unified UI design tokens (colors, geometry, typography).
 * Applied to ImGui each frame via applyThemeToImGui; metrics are design-time
 * units at uiScale=1 and are multiplied by the current UI scale.
 */
struct Theme {
    // --- Colors (RGBA 0..1) ---
    float text[4] = {0.92f, 0.93f, 0.95f, 1.f};
    float textDisabled[4] = {0.50f, 0.52f, 0.55f, 1.f};
    float windowBg[4] = {0.09f, 0.10f, 0.12f, 0.96f};
    float childBg[4] = {0.07f, 0.08f, 0.10f, 0.90f};
    float popupBg[4] = {0.11f, 0.12f, 0.14f, 0.98f};
    float border[4] = {0.28f, 0.30f, 0.35f, 0.70f};
    float frameBg[4] = {0.16f, 0.18f, 0.22f, 1.f};
    float frameBgHovered[4] = {0.22f, 0.26f, 0.32f, 1.f};
    float frameBgActive[4] = {0.26f, 0.32f, 0.40f, 1.f};
    float titleBg[4] = {0.07f, 0.08f, 0.10f, 1.f};
    float titleBgActive[4] = {0.12f, 0.18f, 0.28f, 1.f};
    float titleBgCollapsed[4] = {0.07f, 0.08f, 0.10f, 0.75f};
    float button[4] = {0.22f, 0.45f, 0.78f, 0.70f};
    float buttonHovered[4] = {0.28f, 0.55f, 0.92f, 1.f};
    float buttonActive[4] = {0.16f, 0.38f, 0.72f, 1.f};
    float header[4] = {0.22f, 0.45f, 0.78f, 0.45f};
    float headerHovered[4] = {0.28f, 0.55f, 0.92f, 0.70f};
    float headerActive[4] = {0.22f, 0.45f, 0.78f, 1.f};
    float checkMark[4] = {0.45f, 0.75f, 1.f, 1.f};
    float sliderGrab[4] = {0.35f, 0.60f, 0.95f, 0.90f};
    float sliderGrabActive[4] = {0.45f, 0.70f, 1.f, 1.f};
    float separator[4] = {0.35f, 0.38f, 0.42f, 0.60f};
    float scrollbarBg[4] = {0.05f, 0.06f, 0.07f, 0.55f};
    float scrollbarGrab[4] = {0.35f, 0.38f, 0.42f, 0.80f};
    float scrollbarGrabHovered[4] = {0.45f, 0.48f, 0.52f, 0.90f};
    float scrollbarGrabActive[4] = {0.55f, 0.58f, 0.62f, 1.f};

    // --- Geometry (design units @ scale 1) ---
    float windowRounding = 6.f;
    float childRounding = 4.f;
    float frameRounding = 4.f;
    float popupRounding = 6.f;
    float scrollbarRounding = 8.f;
    float grabRounding = 3.f;
    float tabRounding = 4.f;

    float windowBorderSize = 1.f;
    float childBorderSize = 1.f;
    float popupBorderSize = 1.f;
    float frameBorderSize = 0.f;

    float windowPaddingX = 12.f;
    float windowPaddingY = 10.f;
    float framePaddingX = 8.f;
    float framePaddingY = 5.f;
    float itemSpacingX = 8.f;
    float itemSpacingY = 6.f;
    float itemInnerSpacingX = 6.f;
    float itemInnerSpacingY = 4.f;
    float indentSpacing = 18.f;
    float scrollbarSize = 14.f;
    float grabMinSize = 12.f;

    /** Font size multiplier relative to the base UI font (FontGlobalScale). DPI is
     *  baked into the font atlas at rasterization time, not via FontGlobalScale. */
    float fontScale = 1.f;
    bool navEnableKeyboard = true;

    /** Built-in presets with matching geometry; only colors differ. */
    static Theme dark();
    static Theme light();
};

Theme &globalTheme();
/** Current preset name: "dark", "light", or "custom". */
const std::string &globalThemeName();

void setGlobalTheme(const Theme &theme);
void setGlobalTheme(const Theme &theme, const std::string &name);

/** Apply a named preset ("dark" / "light"). Case-insensitive. Returns false if unknown. */
bool setThemeByName(const std::string &name);

/** UI DPI scale used when applying geometry / font size. */
void setThemeUiScale(float scale);
float themeUiScale();

/** Push tokens into ImGui style. Metrics are multiplied by uiScale (default: themeUiScale()). */
void applyThemeToImGui(const Theme &theme);
void applyThemeToImGui(const Theme &theme, float uiScale);

}  // namespace eve::ui
