#include "devtools/FrameStatsPanel.hpp"

#include "devtools/Immortal.hpp"

#include <cmath>
#include <sstream>

namespace eve::dev {
namespace {
FrameStatsPanel::ImGuiDrawer g_imguiDrawer = nullptr;
constexpr float              kEmaAlpha     = 0.12f;
}  // namespace

FrameStatsPanel& FrameStatsPanel::instance() {
    return Immortal<FrameStatsPanel>::get();
}

void FrameStatsPanel::setVisible(bool on) {
    std::lock_guard lock(mu_);
    visible_ = on;
}

bool FrameStatsPanel::isVisible() const {
    std::lock_guard lock(mu_);
    return visible_;
}

void FrameStatsPanel::toggleVisible() { setVisible(!isVisible()); }

void FrameStatsPanel::sample(float dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.f || dtSeconds > 10.f) return;
    const float instant = 1.f / dtSeconds;
    std::lock_guard lock(mu_);
    lastDt_ = dtSeconds;
    if (emaFps_ <= 0.f)
        emaFps_ = instant;
    else
        emaFps_ = emaFps_ * (1.f - kEmaAlpha) + instant * kEmaAlpha;
}

void FrameStatsPanel::setEntityCount(int count) {
    std::lock_guard lock(mu_);
    entityCount_ = count < 0 ? -1 : count;
}

void FrameStatsPanel::clearEntityCount() {
    std::lock_guard lock(mu_);
    entityCount_ = -1;
}

float FrameStatsPanel::fps() const {
    std::lock_guard lock(mu_);
    return emaFps_;
}

float FrameStatsPanel::frameMs() const {
    std::lock_guard lock(mu_);
    return lastDt_ > 0.f ? lastDt_ * 1000.f : 0.f;
}

int FrameStatsPanel::entityCount() const {
    std::lock_guard lock(mu_);
    return entityCount_;
}

bool FrameStatsPanel::hasEntityCount() const {
    std::lock_guard lock(mu_);
    return entityCount_ >= 0;
}

std::string FrameStatsPanel::format() const {
    std::lock_guard lock(mu_);
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(1);
    oss << emaFps_ << " fps | " << (lastDt_ > 0.f ? lastDt_ * 1000.f : 0.f) << " ms";
    if (entityCount_ >= 0) oss << " | " << entityCount_ << " entities";
    return oss.str();
}

void FrameStatsPanel::setImGuiDrawer(ImGuiDrawer fn) { g_imguiDrawer = fn; }

void FrameStatsPanel::drawImGui() {
    if (g_imguiDrawer) g_imguiDrawer(*this);
}

}  // namespace eve::dev
