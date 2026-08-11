#include "physics/Fluid.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"

#include <algorithm>
#include <cmath>

namespace eve::physics {

Fluid::Fluid(int capacity) : capacity_(capacity) {
    if (capacity_ < 1) throw Exception("Fluid: capacity must be >= 1");
    particles_.reserve(static_cast<size_t>(capacity_));
}

Fluid::~Fluid() { destroy(); }

void Fluid::destroy() {
    destroyed_ = true;
    particles_.clear();
    hash_.clear();
}

void Fluid::setGravity(float gx, float gy) {
    gravityX_ = gx;
    gravityY_ = gy;
}

void Fluid::setSmoothingRadius(float radius) {
    if (radius <= 0.f) throw Exception("Fluid.setSmoothingRadius: radius must be > 0");
    h_ = radius;
}

void Fluid::setRestDensity(float density) {
    if (density <= 0.f) throw Exception("Fluid.setRestDensity: density must be > 0");
    restDensity_ = density;
}

void Fluid::setPressureStiffness(float k) { pressureK_ = std::max(0.f, k); }

void Fluid::setNearPressureStiffness(float k) { nearPressureK_ = std::max(0.f, k); }

void Fluid::setViscosity(float viscosity) { viscosity_ = std::clamp(viscosity, 0.f, 1.f); }

void Fluid::setIterations(int iterations) { iterations_ = std::max(1, iterations); }

void Fluid::setBounds(float x, float y, float w, float h) {
    if (w <= 0.f || h <= 0.f) {
        clearBounds();
        return;
    }
    hasBounds_ = true;
    boundX_ = x;
    boundY_ = y;
    boundW_ = w;
    boundH_ = h;
}

void Fluid::clearBounds() { hasBounds_ = false; }

int Fluid::emit(float x, float y, int count, float vx, float vy) {
    if (destroyed_ || count <= 0) return 0;
    int added = 0;
    const float step = std::max(2.5f, h_ * 0.35f);
    const int side = std::max(1, int(std::ceil(std::sqrt(float(count)))));
    for (int i = 0; i < count && getParticleCount() < capacity_; ++i) {
        Particle p;
        const int cx = i % side;
        const int cy = i / side;
        p.x  = x + (float(cx) - float(side) * 0.5f) * step;
        p.y  = y + (float(cy) - float(side) * 0.5f) * step;
        p.vx = vx;
        p.vy = vy;
        particles_.push_back(p);
        ++added;
    }
    return added;
}

void Fluid::clear() {
    particles_.clear();
    hash_.clear();
}

void Fluid::interactAt(float x, float y, float radius, float strength) {
    interactX_ = x;
    interactY_ = y;
    interactRadius_ = std::max(0.f, radius);
    interactStrength_ = strength;
}

void Fluid::setColor(float r, float g, float b, float a) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    colorA_ = a;
}

void Fluid::setParticleSize(float size) { particleSize_ = std::max(1.f, size); }

bool Fluid::validIndex(int index) const {
    return index >= 0 && index < getParticleCount();
}

float Fluid::getParticleX(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].x;
}

float Fluid::getParticleY(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].y;
}

float Fluid::getParticleVx(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].vx;
}

float Fluid::getParticleVy(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].vy;
}

int64_t Fluid::cellKey(int cx, int cy) const {
    return (int64_t(uint32_t(cx)) << 32) | int64_t(uint32_t(cy));
}

void Fluid::rebuildHash() {
    hash_.clear();
    const float inv = 1.f / h_;
    for (int i = 0; i < getParticleCount(); ++i) {
        const Particle &p = particles_[static_cast<size_t>(i)];
        const int cx = int(std::floor(p.x * inv));
        const int cy = int(std::floor(p.y * inv));
        hash_[cellKey(cx, cy)].push_back(i);
    }
}

void Fluid::applyViscosity(float dt) {
    if (viscosity_ <= 0.f || getParticleCount() == 0) return;
    const float inv = 1.f / h_;
    for (int i = 0; i < getParticleCount(); ++i) {
        Particle &pi = particles_[static_cast<size_t>(i)];
        const int cx = int(std::floor(pi.x * inv));
        const int cy = int(std::floor(pi.y * inv));
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                auto it = hash_.find(cellKey(cx + ox, cy + oy));
                if (it == hash_.end()) continue;
                for (int j : it->second) {
                    if (j <= i) continue;
                    Particle &pj = particles_[static_cast<size_t>(j)];
                    float dx = pj.x - pi.x;
                    float dy = pj.y - pi.y;
                    float r2 = dx * dx + dy * dy;
                    if (r2 >= h_ * h_ || r2 < 1e-8f) continue;
                    const float r = std::sqrt(r2);
                    const float q = 1.f - r / h_;
                    float dvx = pj.vx - pi.vx;
                    float dvy = pj.vy - pi.vy;
                    const float impulse = viscosity_ * q * dt;
                    pi.vx += dvx * impulse * 0.5f;
                    pi.vy += dvy * impulse * 0.5f;
                    pj.vx -= dvx * impulse * 0.5f;
                    pj.vy -= dvy * impulse * 0.5f;
                }
            }
        }
    }
}

void Fluid::doubleDensityRelaxation() {
    const float inv = 1.f / h_;
    for (int i = 0; i < getParticleCount(); ++i) {
        Particle &pi = particles_[static_cast<size_t>(i)];
        float density = 0.f;
        float nearDensity = 0.f;
        const int cx = int(std::floor(pi.x * inv));
        const int cy = int(std::floor(pi.y * inv));

        struct Neighbor {
            int   j;
            float r;
            float dx;
            float dy;
            float q;
        };
        Neighbor neighbors[64];
        int nCount = 0;

        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                auto it = hash_.find(cellKey(cx + ox, cy + oy));
                if (it == hash_.end()) continue;
                for (int j : it->second) {
                    if (j == i) continue;
                    const Particle &pj = particles_[static_cast<size_t>(j)];
                    float dx = pj.x - pi.x;
                    float dy = pj.y - pi.y;
                    float r2 = dx * dx + dy * dy;
                    if (r2 >= h_ * h_ || r2 < 1e-10f) continue;
                    const float r = std::sqrt(r2);
                    const float q = 1.f - r / h_;
                    density += q * q;
                    nearDensity += q * q * q;
                    if (nCount < 64) neighbors[nCount++] = {j, r, dx, dy, q};
                }
            }
        }

        pi.density = density;
        const float pressure = pressureK_ * (density - restDensity_);
        const float nearPressure = nearPressureK_ * nearDensity;

        float dxSum = 0.f;
        float dySum = 0.f;
        for (int n = 0; n < nCount; ++n) {
            const Neighbor &nb = neighbors[n];
            const float mag =
                (pressure * nb.q + nearPressure * nb.q * nb.q) * (1.f / (nb.r + 1e-6f));
            const float dispX = nb.dx * mag * 0.5f;
            const float dispY = nb.dy * mag * 0.5f;
            Particle &pj = particles_[static_cast<size_t>(nb.j)];
            pj.x += dispX;
            pj.y += dispY;
            dxSum -= dispX;
            dySum -= dispY;
        }
        pi.x += dxSum;
        pi.y += dySum;
    }
}

void Fluid::collideBounds() {
    if (!hasBounds_) return;
    const float pad = particleSize_ * 0.5f;
    const float minX = boundX_ + pad;
    const float minY = boundY_ + pad;
    const float maxX = boundX_ + boundW_ - pad;
    const float maxY = boundY_ + boundH_ - pad;
    constexpr float damp = 0.35f;

    for (Particle &p : particles_) {
        if (p.x < minX) {
            p.x = minX;
            p.vx = std::fabs(p.vx) * damp;
        } else if (p.x > maxX) {
            p.x = maxX;
            p.vx = -std::fabs(p.vx) * damp;
        }
        if (p.y < minY) {
            p.y = minY;
            p.vy = std::fabs(p.vy) * damp;
        } else if (p.y > maxY) {
            p.y = maxY;
            p.vy = -std::fabs(p.vy) * damp;
        }
    }
}

void Fluid::update(float dt) {
    if (destroyed_) return;
    if (getParticleCount() == 0) {
        interactStrength_ = 0.f;
        return;
    }
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;

    // External accelerations.
    for (Particle &p : particles_) {
        p.vx += gravityX_ * dt;
        p.vy += gravityY_ * dt;
        if (interactRadius_ > 0.f && interactStrength_ != 0.f) {
            const float dx = interactX_ - p.x;
            const float dy = interactY_ - p.y;
            const float r2 = dx * dx + dy * dy;
            const float R2 = interactRadius_ * interactRadius_;
            if (r2 < R2 && r2 > 1e-6f) {
                const float r = std::sqrt(r2);
                const float w = 1.f - r / interactRadius_;
                p.vx += (dx / r) * interactStrength_ * w * dt;
                p.vy += (dy / r) * interactStrength_ * w * dt;
            }
        }
    }

    rebuildHash();
    applyViscosity(dt);

    // Predict positions, then relax density.
    std::vector<float> prevX(particles_.size()), prevY(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i) {
        prevX[i] = particles_[i].x;
        prevY[i] = particles_[i].y;
        particles_[i].x += particles_[i].vx * dt;
        particles_[i].y += particles_[i].vy * dt;
    }

    for (int iter = 0; iter < iterations_; ++iter) {
        rebuildHash();
        doubleDensityRelaxation();
        collideBounds();
    }

    // Update velocities from position delta.
    const float invDt = dt > 1e-6f ? 1.f / dt : 0.f;
    for (size_t i = 0; i < particles_.size(); ++i) {
        particles_[i].vx = (particles_[i].x - prevX[i]) * invDt;
        particles_[i].vy = (particles_[i].y - prevY[i]) * invDt;
        const float speed2 =
            particles_[i].vx * particles_[i].vx + particles_[i].vy * particles_[i].vy;
        const float maxSpeed = 1600.f;
        if (speed2 > maxSpeed * maxSpeed) {
            const float s = maxSpeed / std::sqrt(speed2);
            particles_[i].vx *= s;
            particles_[i].vy *= s;
        }
    }

    interactStrength_ = 0.f;
}

void Fluid::draw(graphics::Graphics *gfx) {
    if (!gfx || destroyed_) return;
    const float s = particleSize_;
    for (const Particle &p : particles_) {
        const float t = std::clamp(p.density / (restDensity_ * 1.8f), 0.25f, 1.f);
        gfx->drawSolidRect(p.x - s * 0.5f, p.y - s * 0.5f, s, s,
                           Color(colorR_ * (0.55f + 0.45f * t), colorG_ * (0.65f + 0.35f * t),
                                 colorB_, colorA_));
    }
}

}  // namespace eve::physics
