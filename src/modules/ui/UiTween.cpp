#include "ui/UiTween.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::ui {

namespace {

float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }

std::string normalizeEase(const std::string &ease) {
    if (ease.empty()) return "smoothstep";
    return ease;
}

float sampleProgress(double nowMs, double startMs, double delayMs, double durationMs) {
    const double elapsed = nowMs - startMs - delayMs;
    if (elapsed < 0.0) return -1.f;  // still delayed
    if (durationMs <= 0.0) return 1.f;
    if (elapsed >= durationMs) return 1.f;
    return float(elapsed / durationMs);
}

}  // namespace

float evaluateUiEase(float t, const char *kind) {
    t = clamp01(t);
    if (!kind || kind[0] == '\0' || std::strcmp(kind, "smoothstep") == 0)
        return t * t * (3.f - 2.f * t);
    if (std::strcmp(kind, "linear") == 0) return t;
    if (std::strcmp(kind, "inQuad") == 0) return t * t;
    if (std::strcmp(kind, "outQuad") == 0) return 1.f - (1.f - t) * (1.f - t);
    if (std::strcmp(kind, "inOutQuad") == 0)
        return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
    if (std::strcmp(kind, "inCubic") == 0) return t * t * t;
    if (std::strcmp(kind, "outCubic") == 0) return 1.f - std::pow(1.f - t, 3.f);
    if (std::strcmp(kind, "inOutCubic") == 0)
        return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
    if (std::strcmp(kind, "inSine") == 0) return 1.f - std::cos(t * float(M_PI) * 0.5f);
    if (std::strcmp(kind, "outSine") == 0) return std::sin(t * float(M_PI) * 0.5f);
    if (std::strcmp(kind, "inOutSine") == 0) return -(std::cos(float(M_PI) * t) - 1.f) * 0.5f;
    if (std::strcmp(kind, "inExpo") == 0) return t <= 0.f ? 0.f : std::pow(2.f, 10.f * t - 10.f);
    if (std::strcmp(kind, "outExpo") == 0) return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t);
    if (std::strcmp(kind, "inOutExpo") == 0) {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        return t < 0.5f ? std::pow(2.f, 20.f * t - 10.f) * 0.5f
                        : (2.f - std::pow(2.f, -20.f * t + 10.f)) * 0.5f;
    }

    constexpr float backS = 1.70158f;
    if (std::strcmp(kind, "inBack") == 0) return t * t * ((backS + 1.f) * t - backS);
    if (std::strcmp(kind, "outBack") == 0) {
        const float u = t - 1.f;
        return u * u * ((backS + 1.f) * u + backS) + 1.f;
    }
    if (std::strcmp(kind, "inOutBack") == 0) {
        const float s = backS * 1.525f;
        if (t < 0.5f) {
            const float u = t * 2.f;
            return 0.5f * (u * u * ((s + 1.f) * u - s));
        }
        const float u = t * 2.f - 2.f;
        return 0.5f * (u * u * ((s + 1.f) * u + s) + 2.f);
    }

    if (std::strcmp(kind, "inElastic") == 0) {
        if (t <= 0.f || t >= 1.f) return t;
        return -std::pow(2.f, 10.f * t - 10.f) *
               std::sin((t * 10.f - 10.75f) * (2.f * float(M_PI) / 3.f));
    }
    if (std::strcmp(kind, "outElastic") == 0) {
        if (t <= 0.f || t >= 1.f) return t;
        return std::pow(2.f, -10.f * t) *
                   std::sin((t * 10.f - 0.75f) * (2.f * float(M_PI) / 3.f)) +
               1.f;
    }
    if (std::strcmp(kind, "inOutElastic") == 0) {
        if (t <= 0.f || t >= 1.f) return t;
        if (t < 0.5f) {
            return -0.5f * std::pow(2.f, 20.f * t - 10.f) *
                   std::sin((20.f * t - 11.125f) * (2.f * float(M_PI) / 4.5f));
        }
        return std::pow(2.f, -20.f * t + 10.f) *
                   std::sin((20.f * t - 11.125f) * (2.f * float(M_PI) / 4.5f)) * 0.5f +
               1.f;
    }

    auto outBounce = [](float x) {
        constexpr float n1 = 7.5625f;
        constexpr float d1 = 2.75f;
        if (x < 1.f / d1) return n1 * x * x;
        if (x < 2.f / d1) {
            x -= 1.5f / d1;
            return n1 * x * x + 0.75f;
        }
        if (x < 2.5f / d1) {
            x -= 2.25f / d1;
            return n1 * x * x + 0.9375f;
        }
        x -= 2.625f / d1;
        return n1 * x * x + 0.984375f;
    };
    if (std::strcmp(kind, "inBounce") == 0) return 1.f - outBounce(1.f - t);
    if (std::strcmp(kind, "outBounce") == 0) return outBounce(t);
    if (std::strcmp(kind, "inOutBounce") == 0) {
        return t < 0.5f ? (1.f - outBounce(1.f - 2.f * t)) * 0.5f
                        : (1.f + outBounce(2.f * t - 1.f)) * 0.5f;
    }

    // Unknown kind: linear (documented; avoids throwing on script typos).
    return t;
}

double uiTweenWallClockMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool UiTweenDriver::sameHost(UIHostHandle a, UIHostHandle b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}

void UiTweenDriver::replaceHost(UIHostHandle host, UiHostTweenTarget target) {
    hostTweens_.erase(std::remove_if(hostTweens_.begin(), hostTweens_.end(),
                                     [&](const UiHostTween &t) {
                                         return sameHost(t.host, host) && t.target == target;
                                     }),
                      hostTweens_.end());
}

void UiTweenDriver::replaceItem(UIHostHandle host, const std::string &id,
                                UiItemTweenTarget target) {
    itemTweens_.erase(std::remove_if(itemTweens_.begin(), itemTweens_.end(),
                                     [&](const UiItemTween &t) {
                                         return sameHost(t.host, host) && t.nodeId == id &&
                                                t.target == target;
                                     }),
                      itemTweens_.end());
}

void UiTweenDriver::applyHost(UiHostTween &t, float k) {
    auto host = UIHost::resolve(t.host);
    if (!host) {
        t.host = {};
        return;
    }
    auto *m = host->get().meta();
    const float ease = evaluateUiEase(k, t.ease.c_str());
    switch (t.target) {
    case UiHostTweenTarget::Pos:
        m->hasPos = true;
        m->posX = t.fromX + (t.toX - t.fromX) * ease;
        m->posY = t.fromY + (t.toY - t.fromY) * ease;
        break;
    case UiHostTweenTarget::Size:
        m->hasSize = true;
        m->sizeX = t.fromX + (t.toX - t.fromX) * ease;
        m->sizeY = t.fromY + (t.toY - t.fromY) * ease;
        break;
    case UiHostTweenTarget::OverlayAlpha:
        m->overlayBgAlpha = t.fromX + (t.toX - t.fromX) * ease;
        break;
    }
}

void UiTweenDriver::applyItem(UiItemTween &t, float k) {
    auto host = UIHost::resolve(t.host);
    if (!host) {
        t.host = {};
        return;
    }
    auto node = host->get().findById(t.nodeId);
    if (!node) {
        t.host = {};
        return;
    }
    auto &n = node->get();
    const float ease = evaluateUiEase(k, t.ease.c_str());
    switch (t.target) {
    case UiItemTweenTarget::Opacity:
        n.opacity = t.fromX + (t.toX - t.fromX) * ease;
        break;
    case UiItemTweenTarget::Pos:
        n.absolute = true;
        n.posX = t.fromX + (t.toX - t.fromX) * ease;
        n.posY = t.fromY + (t.toY - t.fromY) * ease;
        break;
    }
}

void UiTweenDriver::animateHostPos(UIHostHandle host, float x, float y, float durationMs,
                                   const std::string &ease, float delayMs, double nowMs) {
    auto resolved = UIHost::resolve(host);
    if (!resolved) return;
    auto *m = resolved->get().meta();
    replaceHost(host, UiHostTweenTarget::Pos);
    UiHostTween t;
    t.host = host;
    t.target = UiHostTweenTarget::Pos;
    t.fromX = m->hasPos ? m->posX : 0.f;
    t.fromY = m->hasPos ? m->posY : 0.f;
    t.toX = x;
    t.toY = y;
    t.startMs = nowMs;
    t.durationMs = std::max(0.0, double(durationMs));
    t.delayMs = std::max(0.0, double(delayMs));
    t.ease = normalizeEase(ease);
    m->hasPos = true;
    if (t.durationMs <= 0.0 && t.delayMs <= 0.0) {
        m->posX = t.toX;
        m->posY = t.toY;
        return;
    }
    hostTweens_.push_back(std::move(t));
}

void UiTweenDriver::animateHostSize(UIHostHandle host, float w, float h, float durationMs,
                                    const std::string &ease, float delayMs, double nowMs) {
    auto resolved = UIHost::resolve(host);
    if (!resolved) return;
    auto *m = resolved->get().meta();
    replaceHost(host, UiHostTweenTarget::Size);
    UiHostTween t;
    t.host = host;
    t.target = UiHostTweenTarget::Size;
    t.fromX = m->hasSize ? m->sizeX : 0.f;
    t.fromY = m->hasSize ? m->sizeY : 0.f;
    t.toX = w;
    t.toY = h;
    t.startMs = nowMs;
    t.durationMs = std::max(0.0, double(durationMs));
    t.delayMs = std::max(0.0, double(delayMs));
    t.ease = normalizeEase(ease);
    m->hasSize = true;
    if (t.durationMs <= 0.0 && t.delayMs <= 0.0) {
        m->sizeX = t.toX;
        m->sizeY = t.toY;
        return;
    }
    hostTweens_.push_back(std::move(t));
}

void UiTweenDriver::animateHostOverlayAlpha(UIHostHandle host, float alpha, float durationMs,
                                            const std::string &ease, float delayMs,
                                            double nowMs) {
    auto resolved = UIHost::resolve(host);
    if (!resolved) return;
    auto *m = resolved->get().meta();
    replaceHost(host, UiHostTweenTarget::OverlayAlpha);
    UiHostTween t;
    t.host = host;
    t.target = UiHostTweenTarget::OverlayAlpha;
    t.fromX = m->overlayBgAlpha;
    t.toX = std::clamp(alpha, 0.f, 1.f);
    t.startMs = nowMs;
    t.durationMs = std::max(0.0, double(durationMs));
    t.delayMs = std::max(0.0, double(delayMs));
    t.ease = normalizeEase(ease);
    if (t.durationMs <= 0.0 && t.delayMs <= 0.0) {
        m->overlayBgAlpha = t.toX;
        return;
    }
    hostTweens_.push_back(std::move(t));
}

bool UiTweenDriver::animateItemOpacity(UIHostHandle host, const std::string &id, float opacity,
                                       float durationMs, const std::string &ease, float delayMs,
                                       double nowMs) {
    auto resolved = UIHost::resolve(host);
    if (!resolved) return false;
    auto node = resolved->get().findById(id);
    if (!node) return false;
    replaceItem(host, id, UiItemTweenTarget::Opacity);
    UiItemTween t;
    t.host = host;
    t.nodeId = id;
    t.target = UiItemTweenTarget::Opacity;
    t.fromX = node->get().opacity;
    t.toX = std::clamp(opacity, 0.f, 1.f);
    t.startMs = nowMs;
    t.durationMs = std::max(0.0, double(durationMs));
    t.delayMs = std::max(0.0, double(delayMs));
    t.ease = normalizeEase(ease);
    if (t.durationMs <= 0.0 && t.delayMs <= 0.0) {
        node->get().opacity = t.toX;
        return true;
    }
    itemTweens_.push_back(std::move(t));
    return true;
}

bool UiTweenDriver::animateItemPos(UIHostHandle host, const std::string &id, float x, float y,
                                   float durationMs, const std::string &ease, float delayMs,
                                   double nowMs) {
    auto resolved = UIHost::resolve(host);
    if (!resolved) return false;
    auto node = resolved->get().findById(id);
    if (!node) return false;
    replaceItem(host, id, UiItemTweenTarget::Pos);
    auto &n = node->get();
    UiItemTween t;
    t.host = host;
    t.nodeId = id;
    t.target = UiItemTweenTarget::Pos;
    t.fromX = n.posX;
    t.fromY = n.posY;
    t.toX = x;
    t.toY = y;
    t.startMs = nowMs;
    t.durationMs = std::max(0.0, double(durationMs));
    t.delayMs = std::max(0.0, double(delayMs));
    t.ease = normalizeEase(ease);
    n.absolute = true;
    if (t.durationMs <= 0.0 && t.delayMs <= 0.0) {
        n.posX = t.toX;
        n.posY = t.toY;
        return true;
    }
    itemTweens_.push_back(std::move(t));
    return true;
}

void UiTweenDriver::cancelHost(UIHostHandle host) {
    hostTweens_.erase(
        std::remove_if(hostTweens_.begin(), hostTweens_.end(),
                       [&](const UiHostTween &t) { return sameHost(t.host, host); }),
        hostTweens_.end());
}

void UiTweenDriver::cancelItem(UIHostHandle host, const std::string &id) {
    itemTweens_.erase(std::remove_if(itemTweens_.begin(), itemTweens_.end(),
                                     [&](const UiItemTween &t) {
                                         if (!sameHost(t.host, host)) return false;
                                         return id.empty() || t.nodeId == id;
                                     }),
                      itemTweens_.end());
}

void UiTweenDriver::cancelAll(UIHostHandle host) {
    cancelHost(host);
    cancelItem(host, {});
}

void UiTweenDriver::tick(double nowMs) {
    if (hostTweens_.empty() && itemTweens_.empty()) return;

    for (auto &t : hostTweens_) {
        if (!UIHost::resolve(t.host)) {
            t.host = {};
            continue;
        }
        const float k = sampleProgress(nowMs, t.startMs, t.delayMs, t.durationMs);
        if (k < 0.f) continue;  // delay
        applyHost(t, k);
        if (k >= 1.f) t.host = {};
    }
    hostTweens_.erase(std::remove_if(hostTweens_.begin(), hostTweens_.end(),
                                     [](const UiHostTween &t) {
                                         return !UIHost::resolve(t.host).has_value();
                                     }),
                      hostTweens_.end());

    for (auto &t : itemTweens_) {
        if (!UIHost::resolve(t.host)) {
            t.host = {};
            continue;
        }
        const float k = sampleProgress(nowMs, t.startMs, t.delayMs, t.durationMs);
        if (k < 0.f) continue;
        applyItem(t, k);
        if (k >= 1.f) t.host = {};
    }
    itemTweens_.erase(std::remove_if(itemTweens_.begin(), itemTweens_.end(),
                                     [](const UiItemTween &t) {
                                         return !UIHost::resolve(t.host).has_value();
                                     }),
                      itemTweens_.end());
}

}  // namespace eve::ui
