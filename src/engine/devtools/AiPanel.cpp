#include "devtools/AiPanel.hpp"

#include <chrono>
#include <ctime>
#include <sstream>

#if defined(__has_include)
#if __has_include(<imgui.h>)
#define EVE_AI_PANEL_HAS_IMGUI 1
#include <imgui.h>
#if __has_include(<imgui_internal.h>)
#include <imgui_internal.h>
#define EVE_AI_PANEL_HAS_IMGUI_INTERNAL 1
#endif
#endif
#endif

#ifndef EVE_AI_PANEL_HAS_IMGUI
#define EVE_AI_PANEL_HAS_IMGUI 0
#endif
#ifndef EVE_AI_PANEL_HAS_IMGUI_INTERNAL
#define EVE_AI_PANEL_HAS_IMGUI_INTERNAL 0
#endif

namespace eve::dev {

AiPanel& AiPanel::instance() {
    static AiPanel inst;
    return inst;
}

std::string AiPanel::nowStamp() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

void AiPanel::setVisible(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    visible_ = on;
}

bool AiPanel::isVisible() const {
    std::lock_guard<std::mutex> lock(mu_);
    return visible_;
}

void AiPanel::toggleVisible() { setVisible(!isVisible()); }

void AiPanel::setMcpPort(int port) {
    std::lock_guard<std::mutex> lock(mu_);
    mcpPort_ = port;
}

int AiPanel::mcpPort() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mcpPort_;
}

void AiPanel::setMcpConnected(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    mcpConnected_ = on;
}

bool AiPanel::mcpConnected() const {
    std::lock_guard<std::mutex> lock(mu_);
    return mcpConnected_;
}

void AiPanel::setClientName(std::string name) {
    std::lock_guard<std::mutex> lock(mu_);
    clientName_ = std::move(name);
}

std::string AiPanel::clientName() const {
    std::lock_guard<std::mutex> lock(mu_);
    return clientName_;
}

void AiPanel::clearLog() {
    std::lock_guard<std::mutex> lock(mu_);
    log_.clear();
}

void AiPanel::addLog(std::string kind, std::string title, std::string detail) {
    std::lock_guard<std::mutex> lock(mu_);
    AiLogEntry e;
    e.timestamp = nowStamp();
    e.kind      = std::move(kind);
    e.title     = std::move(title);
    e.detail    = std::move(detail);
    log_.push_back(std::move(e));
    while (log_.size() > maxEntries_) log_.pop_front();
}

void AiPanel::addNote(std::string text) { addLog("note", "note", std::move(text)); }

std::vector<AiLogEntry> AiPanel::recentLog(size_t max) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<AiLogEntry> out;
    if (log_.empty() || max == 0) return out;
    const size_t start = log_.size() > max ? log_.size() - max : 0;
    out.assign(log_.begin() + static_cast<std::ptrdiff_t>(start), log_.end());
    return out;
}

std::string AiPanel::formatLog(size_t max) const {
    auto entries = recentLog(max);
    std::ostringstream oss;
    for (const auto& e : entries) {
        oss << '[' << e.timestamp << "] " << e.kind << " | " << e.title;
        if (!e.detail.empty()) oss << " — " << e.detail;
        oss << '\n';
    }
    return oss.str();
}

std::string AiPanel::statusLine() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::ostringstream oss;
    oss << "MCP ";
    if (mcpPort_ > 0) {
        oss << "127.0.0.1:" << mcpPort_;
        oss << (mcpConnected_ ? " connected" : " listening");
        if (!clientName_.empty()) oss << " (" << clientName_ << ")";
    } else {
        oss << "off";
    }
    oss << " | log " << log_.size();
    return oss.str();
}

void AiPanel::setMaxEntries(size_t n) {
    std::lock_guard<std::mutex> lock(mu_);
    maxEntries_ = n == 0 ? 1 : n;
    while (log_.size() > maxEntries_) log_.pop_front();
}

void AiPanel::drawImGui() {
#if EVE_AI_PANEL_HAS_IMGUI
    if (!isVisible()) return;
    if (ImGui::GetCurrentContext() == nullptr) return;
#if EVE_AI_PANEL_HAS_IMGUI_INTERNAL
    // Avoid Begin() outside NewFrame..Render (games that skip UI this frame).
    if (GImGui == nullptr || !GImGui->WithinFrameScope) return;
#endif

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("EVEngine AI / MCP", &open)) {
        ImGui::End();
        if (!open) setVisible(false);
        return;
    }
    if (!open) {
        setVisible(false);
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(statusLine().c_str());
    ImGui::Separator();
    ImGui::TextWrapped(
        "Agents connect via MCP (eve run --debug --mcp-port). "
        "Tool calls and notes appear below for AI-assisted testing.");

    if (ImGui::Button("Clear log")) clearLog();
    ImGui::SameLine();
    if (ImGui::Button("Hide")) setVisible(false);

    ImGui::BeginChild("ai_log", ImVec2(0, 0), true);
    const auto entries = recentLog(maxEntries_);
    for (const auto& e : entries) {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.f), "[%s]", e.timestamp.c_str());
        ImGui::SameLine();
        ImGui::Text("%s", e.kind.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(e.title.c_str());
        if (!e.detail.empty()) {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextDisabled("%s", e.detail.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    ImGui::End();
#else
    (void)0;
#endif
}

}  // namespace eve::dev
