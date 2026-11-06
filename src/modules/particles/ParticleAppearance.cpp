// eve-hot-path
#include "particles/ParticleAppearance.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace eve::particles {
namespace {
constexpr float kRad2Deg = 180.f / 3.14159265358979323846f;
void            sampleColor(const ParticleEmitter::Config &cfg, float t, Color &out) {
    if (!cfg.colorGradient.empty()) {
        float r, g, b, a;
        cfg.colorGradient.sample(t, r, g, b, a);
        out = Color(r, g, b, a);
        return;
    }
    t     = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    out.r = cfg.colorStart.r + (cfg.colorEnd.r - cfg.colorStart.r) * t;
    out.g = cfg.colorStart.g + (cfg.colorEnd.g - cfg.colorStart.g) * t;
    out.b = cfg.colorStart.b + (cfg.colorEnd.b - cfg.colorStart.b) * t;
    out.a = cfg.colorStart.a + (cfg.colorEnd.a - cfg.colorStart.a) * t;
}

void sampleScale(const ParticleEmitter::Config &cfg, float t, float &scale) {
    if (!cfg.sizeCurve.empty()) {
        scale = cfg.sizeCurve.sample(t, 1.f);
        return;
    }
    t     = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    scale = cfg.sizeStart + (cfg.sizeEnd - cfg.sizeStart) * t;
}

void flipbookUV(const ParticleEmitter::Config &cfg, float frame, float &u0, float &v0, float &u1, float &v1) {
    const auto total = std::int64_t(cfg.hframes) * std::int64_t(cfg.vframes);
    if (total <= 1 || cfg.hframes <= 0 || cfg.vframes <= 0) {
        u0 = 0.f;
        v0 = 0.f;
        u1 = 1.f;
        v1 = 1.f;
        return;
    }
    if (!std::isfinite(frame)) {
        u0 = v0 = u1 = v1 = std::numeric_limits<float>::quiet_NaN();
        return;
    }
    double wrapped = std::fmod(std::floor(double(frame)), double(total));
    if (wrapped < 0) wrapped += double(total);
    const auto  fi  = static_cast<std::int64_t>(wrapped) % total;
    const auto  col = fi % cfg.hframes;
    const auto  row = fi / cfg.hframes;
    const float iw  = 1.f / float(cfg.hframes);
    const float ih  = 1.f / float(cfg.vframes);
    u0              = float(col) * iw;
    v0              = float(row) * ih;
    u1              = u0 + iw;
    v1              = v0 + ih;
}

}  // namespace
Result<ParticleAppearance> evaluateParticleAppearance(const ParticleEmitter::Config &cfg, const Particle &p) {
    const float t = p.lifetime > 0.f ? 1.f - (p.life / p.lifetime) : 1.f;
    Color       c;
    sampleColor(cfg, t, c);
    float scale;
    sampleScale(cfg, t, scale);
    scale *= p.size > 0.f ? p.size : 1.f;
    const float w = cfg.particleW * scale;
    const float h = cfg.particleH * scale;

    ParticleAppearance item;
    item.x = p.x - w * 0.5f;
    item.y = p.y - h * 0.5f;
    item.w = w;
    item.h = h;
    if (cfg.renderMode == "stretched") {
        // Elongate along the velocity direction (comet / streak style).
        const float speed = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        const float len   = std::max(w, speed * cfg.stretchFactor);
        item.w            = len;
        item.rotation     = std::atan2(p.vy, p.vx) * kRad2Deg;
    } else if (cfg.renderMode == "axis") {
        item.rotation = cfg.renderAxisDegrees;
    } else {
        item.rotation = p.rot * kRad2Deg;
        if (!cfg.rotationCurve.empty()) item.rotation += cfg.rotationCurve.sample(t, 0.f);
    }
    item.color = c;
    flipbookUV(cfg, p.frame, item.u0, item.v0, item.u1, item.v1);
    for (const float value : {item.x, item.y, item.w, item.h, item.rotation, item.color.r, item.color.g, item.color.b,
                              item.color.a, item.u0, item.v0, item.u1, item.v1}) {
        if (!std::isfinite(value))
            return Result<ParticleAppearance>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "nonfinite particle appearance", "particleAppearance"));
    }
    return Result<ParticleAppearance>::success(item);
}
}  // namespace eve::particles
