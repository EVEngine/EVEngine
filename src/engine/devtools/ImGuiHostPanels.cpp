#include "devtools/AiPanel.hpp"
#include "devtools/ConsolePanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

// Desktop-only host registration: draws the DevTools AI / console ImGui windows.
// Kept outside EVDevTools (which avoids imgui.h inlines → MSVC 65535 export
// bloat); compiled into the `eve` host executable, where imgui is linked.
//
// The panels are rendered between `ui.beginFrameAndRender()` and
// `gfx.present()` (load.nut calls eve.dev.ai.draw() / eve.dev.console.draw()).

#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)

namespace eve::dev {
namespace {

void drawAiWindow(AiPanel& panel) {
    // Games without an ImGui UI pass still run DevTools/MCP. In that case
    // load.nut invokes this hook after the game render, but ImGui::Begin is
    // invalid until UI::beginFrameAndRender has called ImGui::NewFrame.
    if (!panel.isVisible() || !GImGui || !GImGui->WithinFrameScope) return;
    bool visible = panel.isVisible();
    if (ImGui::Begin("AI / MCP", &visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(panel.statusLine().c_str());
        ImGui::Separator();
        const auto log = panel.recentLog(64);
        if (ImGui::BeginChild("ai_log", ImVec2(480, 260), true)) {
            for (const auto& e : log) {
                ImGui::TextWrapped("[%s] %s | %s", e.timestamp.c_str(), e.kind.c_str(),
                                   e.title.c_str());
                if (!e.detail.empty()) ImGui::TextWrapped("    %s", e.detail.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        if (ImGui::Button("Clear")) panel.clearLog();
        ImGui::SameLine();
        if (ImGui::Button("Hide")) panel.setVisible(false);
    }
    ImGui::End();
    if (!visible) panel.setVisible(false);
}

// Draw the console: scrolling log + Squirrel REPL input line.
void drawConsoleWindow(ConsolePanel& panel) {
    if (!panel.isVisible() || !GImGui || !GImGui->WithinFrameScope) return;
    bool visible = panel.isVisible();
    if (ImGui::Begin("Console", &visible,
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Log")) {
                if (ImGui::MenuItem("Clear")) panel.clear();
                if (ImGui::MenuItem("Copy all"))
                    ImGui::SetClipboardText(panel.format(4096).c_str());
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::BeginChild("console_log", ImVec2(0, ImGui::GetContentRegionAvail().y - 30),
                          true);
        const auto lines = panel.recent(500);
        for (const auto& l : lines) {
            ImVec4 color = ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
            if (l.level == "error")
                color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
            else if (l.level == "warn")
                color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
            else if (l.level == "cmd" || l.level == "result")
                color = ImVec4(0.45f, 0.80f, 1.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(("[" + l.timestamp + "] " + l.level + " | " + l.text).c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        static char input[512] = "";
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
        const bool enter = ImGui::InputText("##repl", input, sizeof(input),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Run") || enter) {
            std::string expr = input;
            if (!expr.empty()) {
                const std::string result = panel.eval(expr);
                ImGui::SetClipboardText(result.c_str());
            }
            input[0] = '\0';
        }
        if (ImGui::Button("Hide")) panel.setVisible(false);
    }
    ImGui::End();
    if (!visible) panel.setVisible(false);
}

struct HostPanels {
    HostPanels() {
        AiPanel::setImGuiDrawer(&drawAiWindow);
        ConsolePanel::setImGuiDrawer(&drawConsoleWindow);
    }
};

}  // namespace
}  // namespace eve::dev

static eve::dev::HostPanels gHostPanels;

#endif
