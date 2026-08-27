#include "physics/Cloth.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"
#include "physics/Body.h"
#include "physics/World.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace eve::physics {

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

namespace {
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

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

void Cloth::reset() {
    if (cols_ < 2 || rows_ < 2) return;
    particles_.clear();
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
    buildLinkKeys();
    pinTopRow();
    grabIndex_ = -1;
    forceX_ = forceY_ = 0.f;
    interactStrength_ = 0.f;
}

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
    buildLinkKeys();
}

void Cloth::buildLinkKeys() {
    linkKeys_.clear();
    for (const Link &link : links_) {
        const int a = link.a;
        const int b = link.b;
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        linkKeys_.insert((int64_t(lo) << 32) | int64_t(hi));
    }
}

bool Cloth::areLinked(int a, int b) const {
    if (a == b) return true;
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    return linkKeys_.find((int64_t(lo) << 32) | int64_t(hi)) != linkKeys_.end();
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

void Cloth::setParticleSize(float size) {
    particleSize_ = std::max(1.f, size);
}

void Cloth::setParticleMass(float mass) {
    particleMass_ = std::max(1e-4f, mass);
}

void Cloth::setSelfCollision(bool on) { selfCollision_ = on; }

void Cloth::setFoldStiffness(float k) {
    foldStiffness_ = std::clamp(k, 0.f, 1.f);
}

void Cloth::setMaxFoldAngle(float degrees) {
    maxFoldAngle_ = std::clamp(degrees, 0.f, 180.f) * kPi / 180.f;
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

void Cloth::interactAt(float x, float y, float radius, float strength) {
    interactX_ = x;
    interactY_ = y;
    interactRadius_ = std::max(0.f, radius);
    interactStrength_ = strength;
}

void Cloth::setCollideWorld(World *world) { world_ = world; }

int64_t Cloth::cellKey(int cx, int cy) const {
    return (int64_t(uint32_t(cx)) << 32) | int64_t(uint32_t(cy));
}

void Cloth::rebuildHash() {
    hash_.clear();
    const float cell = std::max(1e-3f, particleSize_ * 2.f);
    const float inv = 1.f / cell;
    for (int i = 0; i < getParticleCount(); ++i) {
        const Particle &p = particles_[static_cast<size_t>(i)];
        const int cx = int(std::floor(p.x * inv));
        const int cy = int(std::floor(p.y * inv));
        hash_[cellKey(cx, cy)].push_back(i);
    }
}

void Cloth::solveSelfCollision() {
    if (!selfCollision_ || getParticleCount() == 0) return;
    const float minDist = particleSize_ * 2.f;
    if (minDist <= 0.f) return;
    rebuildHash();
    const float cell = std::max(1e-3f, minDist);
    const float inv = 1.f / cell;
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
                    if (areLinked(i, j)) continue;
                    Particle &pj = particles_[static_cast<size_t>(j)];
                    float dx = pj.x - pi.x;
                    float dy = pj.y - pi.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= minDist * minDist || d2 < 1e-8f) continue;
                    const float d = std::sqrt(d2);
                    // Clamp the per-iteration correction so near-coincident
                    // particles cannot explode apart in one substep.
                    const float corr = std::min(0.5f, (minDist - d) / d);
                    if (pi.pinned && pj.pinned) continue;
                    float wa = 0.5f;
                    float wb = 0.5f;
                    // Pinned particles stay fixed; the free partner takes it all.
                    if (pi.pinned && !pj.pinned) { wa = 0.f; wb = 1.f; }
                    else if (pj.pinned && !pi.pinned) { wa = 1.f; wb = 0.f; }
                    pi.x -= dx * corr * wa;
                    pi.y -= dy * corr * wa;
                    pj.x += dx * corr * wb;
                    pj.y += dy * corr * wb;
                }
            }
        }
    }
}

void Cloth::solveFoldConstraint() {
    if (foldStiffness_ <= 0.f || maxFoldAngle_ >= kPi) return;
    // Bend deviation at each interior vertex along rows and columns.
    const float maxDev = maxFoldAngle_;
    const auto straighten = [&](int mid, int a, int b) {
        if (mid < 0 || a < 0 || b < 0 || mid >= getParticleCount() || a >= getParticleCount() ||
            b >= getParticleCount()) {
            return;
        }
        Particle &pm = particles_[static_cast<size_t>(mid)];
        if (pm.pinned) return;
        const Particle &pa = particles_[static_cast<size_t>(a)];
        const Particle &pb = particles_[static_cast<size_t>(b)];
        float v1x = pa.x - pm.x, v1y = pa.y - pm.y;
        float v2x = pb.x - pm.x, v2y = pb.y - pm.y;
        const float l1 = std::sqrt(v1x * v1x + v1y * v1y);
        const float l2 = std::sqrt(v2x * v2x + v2y * v2y);
        if (l1 < 1e-6f || l2 < 1e-6f) return;
        v1x /= l1; v1y /= l1;
        v2x /= l2; v2y /= l2;
        const float dot = std::clamp(v1x * v2x + v1y * v2y, -1.f, 1.f);
        const float angle = std::acos(dot);  // pi = straight
        const float dev = kPi - angle;       // deviation from straight
        if (dev <= maxDev) return;
        // Project the middle particle onto the fold-limit cone: move it toward
        // the midpoint until the deviation equals maxDev (binary search).
        const float mx = (pa.x + pb.x) * 0.5f;
        const float my = (pa.y + pb.y) * 0.5f;
        float lo = 0.f;
        float hi = 1.f;
        for (int it = 0; it < 10; ++it) {
            const float t = (lo + hi) * 0.5f;
            const float qx = pm.x + (mx - pm.x) * t;
            const float qy = pm.y + (my - pm.y) * t;
            const float d1x = pa.x - qx, d1y = pa.y - qy;
            const float d2x = pb.x - qx, d2y = pb.y - qy;
            const float s1 = std::sqrt(d1x * d1x + d1y * d1y);
            const float s2 = std::sqrt(d2x * d2x + d2y * d2y);
            if (s1 < 1e-6f || s2 < 1e-6f) break;
            const float c = std::clamp((d1x * d2x + d1y * d2y) / (s1 * s2), -1.f, 1.f);
            const float devT = kPi - std::acos(c);
            if (devT > maxDev)
                lo = t;
            else
                hi = t;
        }
        const float f = hi * foldStiffness_;
        pm.x += (mx - pm.x) * f;
        pm.y += (my - pm.y) * f;
    };
    for (int r = 0; r < rows_; ++r) {
        for (int c = 1; c + 1 < cols_; ++c) {
            const int mid = r * cols_ + c;
            straighten(mid, mid - 1, mid + 1);
        }
    }
    for (int c = 0; c < cols_; ++c) {
        for (int r = 1; r + 1 < rows_; ++r) {
            const int mid = r * cols_ + c;
            straighten(mid, mid - cols_, mid + cols_);
        }
    }
}

void Cloth::collideWorld(float dt) {
    if (!world_ || !world_->isValid() || particleSize_ <= 0.f) return;
    const float invDt = dt > 1e-6f ? 1.f / dt : 0.f;
    World::ClothContact contact;
    for (int i = 0; i < getParticleCount(); ++i) {
        Particle &p = particles_[static_cast<size_t>(i)];
        if (p.pinned) continue;
        if (!world_->pointProbe(p.x, p.y, particleSize_, &contact) || !contact.hit) continue;
        // Particle velocity (Verlet) before the push.
        const float vpx = (p.x - p.px) * invDt;
        const float vpy = (p.y - p.py) * invDt;
        p.x += contact.nx * contact.depth;
        p.y += contact.ny * contact.depth;

        // Mass-proportional momentum exchange along the contact normal. Static and
        // kinematic bodies act as infinite mass; dynamic bodies take -(1+e)*v_rel
        // scaled by the reduced mass of the particle/body pair.
        float vbx = 0.f;
        float vby = 0.f;
        float bodyMass = 0.f;
        const bool dynamic =
            contact.body != nullptr && contact.body->getType() == "dynamic";
        if (dynamic) {
            vbx = contact.body->getLinearVelocityX();
            vby = contact.body->getLinearVelocityY();
            bodyMass = contact.body->getMass();
        }
        const float vn = (vpx - vbx) * contact.nx + (vpy - vby) * contact.ny;
        if (vn < 0.f) {
            constexpr float restitution = 0.15f;
            const float m = particleMass_;
            const float reduced = bodyMass > 0.f ? (m * bodyMass) / (m + bodyMass) : m;
            float j = -(1.f + restitution) * vn * reduced;
            // Cap the particle velocity kick to keep the solver stable.
            const float maxKick = 900.f;  // pixels/s
            j = std::min(j, maxKick * m);
            const float kick = (j / m) * dt;
            p.x += contact.nx * kick;
            p.y += contact.ny * kick;
            if (dynamic && bodyMass > 0.f) {
                contact.body->applyLinearImpulse(-contact.nx * j, -contact.ny * j);
            }
        }
    }
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
    const bool hasInteract = interactRadius_ > 0.f && interactStrength_ != 0.f;

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
        if (hasInteract) {
            const float dx = interactX_ - p.x;
            const float dy = interactY_ - p.y;
            const float r2 = dx * dx + dy * dy;
            const float R2 = interactRadius_ * interactRadius_;
            if (r2 < R2 && r2 > 1e-6f) {
                const float r = std::sqrt(r2);
                const float w = 1.f - r / interactRadius_;
                p.x += (dx / r) * interactStrength_ * w * dt * dt;
                p.y += (dy / r) * interactStrength_ * w * dt * dt;
            }
        }
        // Cap the per-substep displacement so dense pile-ups cannot accumulate
        // unbounded kinetic energy (mirrors Fluid::update's maxSpeed clamp).
        constexpr float maxSpeed = 900.f;  // pixels/s
        const float maxDisp = maxSpeed * dt;
        const float dvx = p.x - p.px;
        const float dvy = p.y - p.py;
        const float v2 = dvx * dvx + dvy * dvy;
        if (v2 > maxDisp * maxDisp) {
            const float s = maxDisp / std::sqrt(v2);
            p.px = p.x - dvx * s;
            p.py = p.y - dvy * s;
        }
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

    updateSubsteps(dt, 2);
}

void Cloth::updateSubsteps(float dt, int substeps) {
    if (destroyed_ || substeps < 1) return;

    // Fixed substeps for stability under hitch frames.
    const float h = dt / float(substeps);
    for (int s = 0; s < substeps; ++s) {
        integrate(h);
        solveConstraints();
        solveFoldConstraint();
        solveSelfCollision();
        collideWorld(h);
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
    interactStrength_ = 0.f;
}

eve::Result<void> Cloth::step(const eve::SimulationStep& stepValue,
                             const SimulationSettings& settings) {
    if (destroyed_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "Cannot step a destroyed cloth", "physics.cloth.step"));
    auto valid = detail::validateSimulationStep(stepValue, settings, observation_);
    if (!valid) return valid;
    auto next = detail::advanceSimulationObservation(observation_, stepValue);
    if (!next) return eve::Result<void>::failure(next.status());
    try {
        updateSubsteps(static_cast<float>(stepValue.delta.seconds()), settings.subStepCount);
    } catch (const std::exception& error) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed,
            std::string("Cloth step failed: ") + error.what(), "physics.cloth.step"));
    } catch (...) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "Cloth step failed with an unknown exception",
            "physics.cloth.step"));
    }
    observation_ = std::move(next).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Cloth::restoreObservation(const SimulationObservation& observation) {
    auto valid = detail::validateSimulationObservation(
        observation, "physics.cloth.restoreObservation");
    if (!valid) return valid;
    if (destroyed_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "Cannot restore a destroyed cloth", "physics.cloth.restoreObservation"));
    observation_ = observation;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
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
        const float s = p.pinned ? std::max(5.f, particleSize_ + 2.f) : particleSize_;
        gfx->drawSolidRect(p.x - s * 0.5f, p.y - s * 0.5f, s, s, nodeColor);
    }
    if (grabIndex_ >= 0) {
        const Particle &p = particles_[static_cast<size_t>(grabIndex_)];
        gfx->drawSolidRect(p.x - 6.f, p.y - 6.f, 12.f, 12.f,
                           Color(1.f, 0.85f, 0.35f, 0.9f));
    }
}

}  // namespace eve::physics
