#include "ui/Theme.h"

#include <imgui.h>

namespace eve::ui {
namespace {

Theme g_theme;

}  // namespace

Theme &globalTheme() { return g_theme; }

void setGlobalTheme(const Theme &theme) { g_theme = theme; }

void applyThemeToImGui(const Theme &theme) {
    ImGuiStyle &style = ImGui::GetStyle();
    style.FrameRounding = theme.frameRounding;
    style.Colors[ImGuiCol_WindowBg] =
        ImVec4(theme.windowBg[0], theme.windowBg[1], theme.windowBg[2], theme.windowBg[3]);
    style.Colors[ImGuiCol_Text] = ImVec4(theme.text[0], theme.text[1], theme.text[2], theme.text[3]);
    style.Colors[ImGuiCol_Button] =
        ImVec4(theme.button[0], theme.button[1], theme.button[2], theme.button[3]);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(theme.buttonHovered[0], theme.buttonHovered[1],
                                                  theme.buttonHovered[2], theme.buttonHovered[3]);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(theme.buttonActive[0], theme.buttonActive[1],
                                                 theme.buttonActive[2], theme.buttonActive[3]);

    ImGuiIO &io = ImGui::GetIO();
    if (theme.navEnableKeyboard) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

}  // namespace eve::ui
