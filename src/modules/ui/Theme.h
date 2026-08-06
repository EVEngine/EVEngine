#pragma once

#include <cstdint>

namespace eve::ui {

/** Simple style tokens applied before walking hosts each frame. */
struct Theme {
    float windowBg[4] = {0.06f, 0.06f, 0.07f, 0.94f};
    float text[4] = {1.f, 1.f, 1.f, 1.f};
    float button[4] = {0.26f, 0.59f, 0.98f, 0.40f};
    float buttonHovered[4] = {0.26f, 0.59f, 0.98f, 1.f};
    float buttonActive[4] = {0.06f, 0.53f, 0.98f, 1.f};
    float frameRounding = 3.f;
    bool navEnableKeyboard = true;
};

Theme &globalTheme();
void applyThemeToImGui(const Theme &theme);
void setGlobalTheme(const Theme &theme);

}  // namespace eve::ui
