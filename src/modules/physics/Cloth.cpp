#include "physics/Cloth.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"

#include <algorithm>
#include <cmath>

namespace eve::physics {

Cloth::Cloth(int cols, int rows, float spacing, float originX, float originY)
    : cols_(cols), rows_(rows), spacing_(spacing), originX_(originX), originY_(originY) {
    if (cols_ < 2 || rows_ < 2)
        throw Exception("Cloth: cols and rows must be >= 2");
    if (spacing_ <= 0.f)
        throw Exception("Cloth: spacing must be > 0");

    particles_.resize(static_cast<size_t>(cols_ * rows_));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            Particle &p = particles_[static_cast<size_t>(r * cols_ + c)];
            p.x = p.px = originX_ + float(c) * spacing_;
            p.y = p.py = originY_ + float(r) * spacing_;
            p.pinned = false;
        }
    }
    rebuildLinks();
    pinTopRow();
}

Cloth::~Cloth() { destroy(); }

void Cloth::destroy() { destroyed_ = true; }

void Cloth::rebuildLinks() {
    links_.clear();
    auto add = [&](int a, int b) {
        if (a < 0 || b < 0 || a >= getParticleCount() || b >= getParticleCount()) return;
        Link link;
        link.a    = a;
        link.b    = b;
        const Particle &pa = particles_[static_cast<size_t>(a)];
        const Particle &pb = particles_[static_cast<size_t>(b)];
        const float dx = pb.x - pa.x;
        const float dy = pb.y - pa.y;
        link.rest = std::sqrt(dx * dx + dy * dy);
        if (link.rest > 1e-4f) links_.push_back(link);
    };

    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const int i = r * cols_ + c;
            // Structural
            if (c + 1 < cols_) add(i, i + 1);
            if (r + 1 < rows_) add(i, i + cols_);
            // Shear
            if (c + 1 < cols_ && r + 1 < rows_) add(i, i + cols_ + 1);
            if (c > 0 && r + 1 < rows_) add(i, i + cols_ - 1);
            // Bend (every other)
            if (c + 2 < cols_) add(i, i + 2);
            if (r + 2 < rows_) add(i, i + cols_ * 2);
        }
    }
}

bool Cloth::validIndex(int index) const {
    return index >= 0 && index < getParticleCount();
}

void Cloth::setGravity(float gx, float gy) {
    gravityX_ = gx;
    gravityY_ = gy;
}

void Cloth::setStiffness(float stiffness) {
    stiffness_ = std::clamp(stiffness, 0.f, 1.f);
}

void Cloth::setIterations(int iterations) {
    iterations_ = std::max(1, iterations);
}

void Cloth::setDamping(float damping) {
    damping_ = std::clamp(damping, 0.f, 1.f);
}

void Cloth::setBounds(float x, float y, float w, float h) {
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

void Cloth::clearBounds() { hasBounds_ = false; }

void Cloth::pin(int index) {
    if (!validIndex(index)) throw Exception("Cloth.pin: index out of range");
    particles_[static_cast<size_t>(index)].pinned = true;
}

void Cloth::unpin(int index) {
    if (!validIndex(index)) throw Exception("Cloth.unpin: index out of range");
    particles_[static_cast<size_t>(index)].pinned = false;
}

void Cloth::pinTopRow() {
    for (int c = 0; c < cols_; ++c)
        particles_[static_cast<size_t>(c)].pinned = true;
}

bool Cloth::isPinned(int index) const {
    if (!validIndex(index)) return false;
    return particles_[static_cast<size_t>(index)].pinned;
}

int Cloth::grabAt(float x, float y, float radius) {
    grabIndex_ = -1;
    float best = radius * radius;
    for (int i = 0; i < getParticleCount(); ++i) {
        const Particle &p = particles_[static_cast<size_t>(i)];
        if (p.pinned) continue;
        const float dx = p.x - x;
        const float dy = p.y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= best) {
            best = d2;
            grabIndex_ = i;
        }
    }
    if (grabIndex_ >= 0) moveGrab(x, y);
    return grabIndex_;
}

void Cloth::moveGrab(float x, float y) {
    grabX_ = x;
    grabY_ = y;
    if (!validIndex(grabIndex_)) return;
    Particle &p = particles_[static_cast<size_t>(grabIndex_)];
    p.x = x;
    p.y = y;
    p.px = x;
    p.py = y;
}

void Cloth::releaseGrab() { grabIndex_ = -1; }

void Cloth::applyForce(float fx, float fy) {
    forceX_ += fx;
    forceY_ += fy;
}

void Cloth::setColor(float r, float g, float b, float a) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    colorA_ = a;
}

float Cloth::getParticleX(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].x;
}

float Cloth::getParticleY(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].y;
}

void Cloth::setParticlePosition(int index, float x, float y) {
    if (!validIndex(index)) throw Exception("Cloth.setParticlePosition: index out of range");
    Particle &p = particles_[static_cast<size_t>(index)];
    p.x = p.px = x;
    p.y = p.py = y;
}

void Cloth::integrate(float dt) {
    if (dt <= 0.f) return;
    const float ax = gravityX_ + forceX_;
    const float ay = gravityY_ + forceY_;
    const float damp = 1.f - damping_;

    for (Particle &p : particles_) {
        if (p.pinned) {
            p.px = p.x;
            p.py = p.y;
            continue;
        }
        const float vx = (p.x - p.px) * damp;
        const float vy = (p.y - p.py) * damp;
        p.px = p.x;
        p.py = p.y;
        p.x += vx + ax * dt * dt;
        p.y += vy + ay * dt * dt;
    }
}

void Cloth::solveConstraints() {
    for (int iter = 0; iter < iterations_; ++iter) {
        for (const Link &link : links_) {
            Particle &a = particles_[static_cast<size_t>(link.a)];
            Particle &b = particles_[static_cast<size_t>(link.b)];
            if (a.pinned && b.pinned) continue;
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 1e-5f) continue;
            const float diff = (dist - link.rest) / dist * stiffness_;
            // When one end is pinned, apply the full correction to the free end.
            if (a.pinned) {
                b.x -= dx * diff;
                b.y -= dy * diff;
            } else if (b.pinned) {
                a.x += dx * diff;
                a.y += dy * diff;
            } else {
                const float half = diff * 0.5f;
                a.x += dx * half;
                a.y += dy * half;
                b.x -= dx * half;
                b.y -= dy * half;
            }
        }
        if (validIndex(grabIndex_)) {
            Particle &g = particles_[static_cast<size_t>(grabIndex_)];
            g.x  = grabX_;
            g.y  = grabY_;
            g.px = grabX_;
            g.py = grabY_;
        }
    }
}

void Cloth::collideBounds() {
    if (!hasBounds_) return;
    const float minX = boundX_;
    const float minY = boundY_;
    const float maxX = boundX_ + boundW_;
    const float maxY = boundY_ + boundH_;
    constexpr float bounce = 0.35f;

    for (Particle &p : particles_) {
        if (p.pinned) continue;
        if (p.x < minX) {
            const float vx = p.x - p.px;
            p.x  = minX;
            p.px = p.x + vx * bounce;
        } else if (p.x > maxX) {
            const float vx = p.x - p.px;
            p.x  = maxX;
            p.px = p.x + vx * bounce;
        }
        if (p.y < minY) {
            const float vy = p.y - p.py;
            p.y  = minY;
            p.py = p.y + vy * bounce;
        } else if (p.y > maxY) {
            const float vy = p.y - p.py;
            p.y  = maxY;
            p.py = p.y + vy * bounce;
        }
    }
}

void Cloth::update(float dt) {
    if (destroyed_) return;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;

    // Fixed substeps for stability under hitch frames.
    const int substeps = 2;
    const float h = dt / float(substeps);
    for (int s = 0; s < substeps; ++s) {
        integrate(h);
        solveConstraints();
        collideBounds();
        if (grabIndex_ >= 0) {
            Particle &p = particles_[static_cast<size_t>(grabIndex_)];
            // Preserve grab target set by moveGrab (px/py already synced there).
            p.px = p.x;
            p.py = p.y;
        }
    }
    forceX_ = 0.f;
    forceY_ = 0.f;
}

void Cloth::draw(graphics::Graphics *gfx) {
    if (!gfx || destroyed_) return;
    const Color linkColor(colorR_, colorG_, colorB_, colorA_ * 0.75f);
    const Color nodeColor(colorR_, colorG_, colorB_, colorA_);

    for (const Link &link : links_) {
        const Particle &a = particles_[static_cast<size_t>(link.a)];
        const Particle &b = particles_[static_cast<size_t>(link.b)];
        const float x1 = a.x, y1 = a.y, x2 = b.x, y2 = b.y;
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(len / 4.f));
        for (int i = 0; i <= steps; ++i) {
            const float t = float(i) / float(steps);
            gfx->drawSolidRect(x1 + dx * t - 1.f, y1 + dy * t - 1.f, 2.f, 2.f, linkColor);
        }
    }
    for (const Particle &p : particles_) {
        const float s = p.pinned ? 5.f : 3.f;
        gfx->drawSolidRect(p.x - s * 0.5f, p.y - s * 0.5f, s, s, nodeColor);
    }
    if (grabIndex_ >= 0) {
        const Particle &p = particles_[static_cast<size_t>(grabIndex_)];
        gfx->drawSolidRect(p.x - 6.f, p.y - 6.f, 12.f, 12.f,
                           Color(1.f, 0.85f, 0.35f, 0.9f));
    }
}

}  // namespace eve::physics
