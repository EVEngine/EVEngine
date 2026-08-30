#include "devtools/AiPanel.hpp"
#include "devtools/Immortal.hpp"

#include <chrono>
#include <ctime>
#include <sstream>

namespace eve::dev {
namespace {

AiPanel::ImGuiDrawer g_imguiDrawer = nullptr;

}  // namespace

AiPanel& AiPanel::instance() {
    // Process-immortal singleton: McpServer's detached stdio reader and the
    // other DevTools singletons may still touch AiPanel during teardown, so
    // destroying it would let them lock a destroyed mutex (libc++ aborts).
    // See devtools/Immortal.hpp for the shared contract.
    return Immortal<AiPanel>::get();
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

void AiPanel::setImGuiDrawer(ImGuiDrawer fn) { g_imguiDrawer = fn; }

void AiPanel::drawImGui() {
    // ImGui UI lives in eve_imgui (ImGuiBackend) so EVDevTools does not
    // instantiate imgui.h inlines — that blew the MSVC 65535 export limit.
    if (g_imguiDrawer) g_imguiDrawer(*this);
}

}  // namespace eve::dev
