#include "physics/Cloth3D.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"
#include "graphics/Mesh.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::physics {

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
using eve::graphics::Mesh;

namespace {
constexpr float kPi = 3.14159265358979323846f;

int64_t pairKey(int lo, int hi) {
    return (int64_t(lo) << 32) | int64_t(hi);
}

// Minimal 3D vector helpers for the triangle-level self-collision pass.
struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3 sub(const V3 &a, const V3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 add(const V3 &a, const V3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 mul(float s, const V3 &a) { return {s * a.x, s * a.y, s * a.z}; }
V3 fma(const V3 &a, float s, const V3 &b) { return {a.x * s + b.x, a.y * s + b.y, a.z * s + b.z}; }
float dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3 &a, const V3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float vlen(const V3 &a) { return std::sqrt(dot(a, a)); }
V3 vnorm(const V3 &a) {
    const float l = vlen(a);
    return l > 1e-8f ? mul(1.f / l, a) : V3{0.f, 1.f, 0.f};
}

// Rotate vector v around the given unit axis by angle (right-hand rule).
V3 rotateAround(const V3 &v, const V3 &axis, float angle) {
    const float c = std::cos(angle);
    const float sn = std::sin(angle);
    const V3 cv = cross(axis, v);
    const float d = dot(axis, v);
    return add(add(mul(c, v), mul(sn, cv)), mul(d * (1.f - c), axis));
}

// Signed angle from a to b around the unit axis u (positive = CCW along +u).
float signedAngle(const V3 &a, const V3 &b, const V3 &u) {
    return std::atan2(dot(cross(a, b), u), dot(a, b));
}

// Closest point on segment [a,b] to point p (Ericson, RTCD ch. 5).
V3 closestPointSegment(const V3 &a, const V3 &b, const V3 &p) {
    const V3 ab = sub(b, a);
    const float len2 = dot(ab, ab);
    if (len2 < 1e-12f) return a;
    const float t = std::clamp(dot(sub(p, a), ab) / len2, 0.f, 1.f);
    return fma(ab, t, a);
}

// Closest point on triangle (a,b,c) to point p (Ericson, RTCD ch. 5).
V3 closestPointTriangle(const V3 &a, const V3 &b, const V3 &c, const V3 &p) {
    const V3 ab = sub(b, a);
    const V3 ac = sub(c, a);
    const V3 ap = sub(p, a);
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return a;

    const V3 bp = sub(p, b);
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float t = d1 / (d1 - d3);
        return fma(ab, t, a);
    }

    const V3 cp = sub(p, c);
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float t = d2 / (d2 - d6);
        return fma(ac, t, a);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return fma(sub(c, b), t, b);
    }

    const float denom = 1.f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return add(fma(ab, v, a), mul(w, ac));
}

// Closest points between segments (p1,p2) and (q1,q2); returns distance.
float closestPointSegments(const V3 &p1, const V3 &p2, const V3 &q1, const V3 &q2, V3 &cp1,
                           V3 &cp2) {
    const V3 d1 = sub(p2, p1);
    const V3 d2 = sub(q2, q1);
    const V3 r = sub(p1, q1);
    const float a = dot(d1, d1);
    const float e = dot(d2, d2);
    const float f = dot(d2, r);
    float s = 0.f;
    float t = 0.f;
    if (a <= 1e-12f && e <= 1e-12f) {
        s = t = 0.f;
    } else if (a <= 1e-12f) {
        s = 0.f;
        t = std::clamp(f / e, 0.f, 1.f);
    } else {
        const float c = dot(d1, r);
        if (e <= 1e-12f) {
            t = 0.f;
            s = std::clamp(-c / a, 0.f, 1.f);
        } else {
            const float b = dot(d1, d2);
            const float denom = a * e - b * b;
            s = denom > 1e-12f ? std::clamp((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
            t = (b * s + f) / e;
            if (t < 0.f) {
                t = 0.f;
                s = std::clamp(-c / a, 0.f, 1.f);
            } else if (t > 1.f) {
                t = 1.f;
                s = std::clamp((b - c) / a, 0.f, 1.f);
            }
        }
    }
    cp1 = fma(d1, s, p1);
    cp2 = fma(d2, t, q1);
    return vlen(sub(cp1, cp2));
}

bool pointInTriangle(const V3 &p, const V3 &a, const V3 &b, const V3 &c) {
    const V3 n = cross(sub(b, a), sub(c, a));
    const V3 pa = cross(sub(b, a), sub(p, a));
    const V3 pb = cross(sub(c, b), sub(p, b));
    const V3 pc = cross(sub(a, c), sub(p, c));
    return dot(pa, n) >= -1e-8f && dot(pb, n) >= -1e-8f && dot(pc, n) >= -1e-8f;
}

// Closest distance between two triangles plus the indices of the vertices
// forming the closest feature on each side (1, 2 or 3 vertices).
float triangleDistance(const V3 *A, const V3 *B, int featA[3], int featB[3], int &countA,
                       int &countB, V3 &cpA, V3 &cpB) {
    float best = std::numeric_limits<float>::max();
    countA = countB = 0;
    for (int i = 0; i < 3; ++i) {
        const V3 cp = closestPointTriangle(B[0], B[1], B[2], A[i]);
        const float d = vlen(sub(A[i], cp));
        if (d < best) {
            best = d;
            cpA = A[i];
            cpB = cp;
            featA[0] = i;
            featB[0] = 0;
            featB[1] = 1;
            featB[2] = 2;
            countA = 1;
            countB = 3;
        }
    }
    for (int j = 0; j < 3; ++j) {
        const V3 cp = closestPointTriangle(A[0], A[1], A[2], B[j]);
        const float d = vlen(sub(B[j], cp));
        if (d < best) {
            best = d;
            cpA = cp;
            cpB = B[j];
            featA[0] = 0;
            featA[1] = 1;
            featA[2] = 2;
            featB[0] = j;
            countA = 3;
            countB = 1;
        }
    }
    const int aEdges[3][2] = {{0, 1}, {1, 2}, {2, 0}};
    const int bEdges[3][2] = {{0, 1}, {1, 2}, {2, 0}};
    for (const auto &ea : aEdges) {
        for (const auto &eb : bEdges) {
            V3 cp1;
            V3 cp2;
            const float d = closestPointSegments(A[ea[0]], A[ea[1]], B[eb[0]], B[eb[1]], cp1,
                                                 cp2);
            if (d < best) {
                best = d;
                cpA = cp1;
                cpB = cp2;
                featA[0] = ea[0];
                featA[1] = ea[1];
                featB[0] = eb[0];
                featB[1] = eb[1];
                countA = 2;
                countB = 2;
            }
        }
    }
    return best;
}
}  // namespace

Cloth3D::Cloth3D(int cols, int rows, float spacing, float originX, float originY, float originZ)
    : cols_(cols), rows_(rows), spacing_(spacing), originX_(originX), originY_(originY),
      originZ_(originZ) {
    if (cols_ < 2 || rows_ < 2)
        throw Exception("Cloth3D: cols and rows must be >= 2");
    if (spacing_ <= 0.f)
        throw Exception("Cloth3D: spacing must be > 0");

    particles_.resize(static_cast<size_t>(cols_ * rows_));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            Particle &p = particles_[static_cast<size_t>(r * cols_ + c)];
            p.x = p.px = originX_ + float(c) * spacing_;
            p.y = p.py = originY_;
            p.z = p.pz = originZ_ + float(r) * spacing_;
            p.pinned = false;
        }
    }
    rebuildLinks();
    rebuildTriangles();
    pinTopRow();
}

Cloth3D::~Cloth3D() { destroy(); }

void Cloth3D::destroy() { destroyed_ = true; }

void Cloth3D::reset() {
    if (cols_ < 2 || rows_ < 2) return;
    particles_.clear();
    particles_.resize(static_cast<size_t>(cols_ * rows_));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            Particle &p = particles_[static_cast<size_t>(r * cols_ + c)];
            p.x = p.px = originX_ + float(c) * spacing_;
            p.y = p.py = originY_;
            p.z = p.pz = originZ_ + float(r) * spacing_;
            p.pinned = false;
        }
    }
    rebuildLinks();
    rebuildTriangles();
    pinTopRow();
    grabIndex_ = -1;
    forceX_ = forceY_ = forceZ_ = 0.f;
    interactStrength_ = 0.f;
}

void Cloth3D::rebuildLinks() {
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
        const float dz = pb.z - pa.z;
        link.rest = std::sqrt(dx * dx + dy * dy + dz * dz);
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

void Cloth3D::buildLinkKeys() {
    linkKeys_.clear();
    for (const Link &link : links_) {
        const int lo = std::min(link.a, link.b);
        const int hi = std::max(link.a, link.b);
        linkKeys_.insert(pairKey(lo, hi));
    }
}

bool Cloth3D::areLinked(int a, int b) const {
    if (a == b) return true;
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    return linkKeys_.find(pairKey(lo, hi)) != linkKeys_.end();
}

void Cloth3D::rebuildTriangles() {
    triangles_.clear();
    foldPairs_.clear();

    // Two triangles per quad, wound so the +Y side is the front face.
    std::unordered_map<int64_t, std::vector<int>> edgeTris;
    auto addTri = [&](int a, int b, int c, std::unordered_map<int64_t, std::vector<int>> &map) {
        Tri tri;
        tri.v[0] = a;
        tri.v[1] = b;
        tri.v[2] = c;
        const int idx = static_cast<int>(triangles_.size());
        const int edges[3][2] = {{a, b}, {b, c}, {c, a}};
        for (const auto &e : edges) {
            map[pairKey(std::min(e[0], e[1]), std::max(e[0], e[1]))].push_back(idx);
        }
        triangles_.push_back(tri);
    };

    for (int r = 0; r + 1 < rows_; ++r) {
        for (int c = 0; c + 1 < cols_; ++c) {
            const int a = r * cols_ + c;
            const int b = r * cols_ + c + 1;
            const int cc = (r + 1) * cols_ + c + 1;
            const int d = (r + 1) * cols_ + c;
            addTri(a, d, b, edgeTris);
            addTri(d, cc, b, edgeTris);
        }
    }

    for (const auto &entry : edgeTris) {
        if (entry.second.size() != 2) continue;
        const Tri &t0 = triangles_[static_cast<size_t>(entry.second[0])];
        const Tri &t1 = triangles_[static_cast<size_t>(entry.second[1])];
        const int e0 = int(entry.first >> 32);
        const int e1 = static_cast<int>(static_cast<uint32_t>(entry.first));
        // Shared edge endpoints e0/e1; opposite vertices are the third of each tri.
        int k = -1;
        for (int v : t0.v) {
            if (v != e0 && v != e1) { k = v; break; }
        }
        int l = -1;
        for (int v : t1.v) {
            if (v != e0 && v != e1) { l = v; break; }
        }
        if (k < 0 || l < 0) continue;
        FoldPair pair;
        pair.a = e0;
        pair.b = e1;
        pair.k = k;
        pair.l = l;
        foldPairs_.push_back(pair);
    }
}

bool Cloth3D::validIndex(int index) const {
    return index >= 0 && index < getParticleCount();
}

void Cloth3D::setGravity(float gx, float gy, float gz) {
    gravityX_ = gx;
    gravityY_ = gy;
    gravityZ_ = gz;
}

void Cloth3D::setStiffness(float stiffness) {
    stiffness_ = std::clamp(stiffness, 0.f, 1.f);
}

void Cloth3D::setIterations(int iterations) {
    iterations_ = std::max(1, iterations);
}

void Cloth3D::setDamping(float damping) {
    damping_ = std::clamp(damping, 0.f, 1.f);
}

void Cloth3D::setParticleSize(float size) {
    particleSize_ = std::max(0.01f, size);
}

void Cloth3D::setParticleMass(float mass) {
    particleMass_ = std::max(1e-4f, mass);
}

void Cloth3D::setSelfCollision(bool on) { selfCollision_ = on; }

void Cloth3D::setFoldStiffness(float k) {
    foldStiffness_ = std::clamp(k, 0.f, 1.f);
}

void Cloth3D::setMaxFoldAngle(float degrees) {
    maxFoldAngle_ = std::clamp(degrees, 0.f, 180.f) * kPi / 180.f;
}

void Cloth3D::setBounds(float x, float y, float z, float w, float h, float d) {
    if (w <= 0.f || h <= 0.f || d <= 0.f) {
        clearBounds();
        return;
    }
    hasBounds_ = true;
    boundX_ = x;
    boundY_ = y;
    boundZ_ = z;
    boundW_ = w;
    boundH_ = h;
    boundD_ = d;
}

void Cloth3D::clearBounds() { hasBounds_ = false; }

void Cloth3D::pin(int index) {
    if (!validIndex(index)) throw Exception("Cloth3D.pin: index out of range");
    particles_[static_cast<size_t>(index)].pinned = true;
}

void Cloth3D::unpin(int index) {
    if (!validIndex(index)) throw Exception("Cloth3D.unpin: index out of range");
    particles_[static_cast<size_t>(index)].pinned = false;
}

void Cloth3D::pinTopRow() {
    for (int c = 0; c < cols_; ++c)
        particles_[static_cast<size_t>(c)].pinned = true;
}

bool Cloth3D::isPinned(int index) const {
    if (!validIndex(index)) return false;
    return particles_[static_cast<size_t>(index)].pinned;
}

int Cloth3D::grabAt(float x, float y, float z, float radius) {
    grabIndex_ = -1;
    float best = radius * radius;
    for (int i = 0; i < getParticleCount(); ++i) {
        const Particle &p = particles_[static_cast<size_t>(i)];
        if (p.pinned) continue;
        const float dx = p.x - x;
        const float dy = p.y - y;
        const float dz = p.z - z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best) {
            best = d2;
            grabIndex_ = i;
        }
    }
    if (grabIndex_ >= 0) moveGrab(x, y, z);
    return grabIndex_;
}

void Cloth3D::moveGrab(float x, float y, float z) {
    grabX_ = x;
    grabY_ = y;
    grabZ_ = z;
    if (!validIndex(grabIndex_)) return;
    Particle &p = particles_[static_cast<size_t>(grabIndex_)];
    p.x = x;
    p.y = y;
    p.z = z;
    p.px = x;
    p.py = y;
    p.pz = z;
}

void Cloth3D::releaseGrab() { grabIndex_ = -1; }

void Cloth3D::applyForce(float fx, float fy, float fz) {
    forceX_ += fx;
    forceY_ += fy;
    forceZ_ += fz;
}

void Cloth3D::interactAt(float x, float y, float z, float radius, float strength) {
    interactX_ = x;
    interactY_ = y;
    interactZ_ = z;
    interactRadius_ = std::max(0.f, radius);
    interactStrength_ = strength;
}

void Cloth3D::setCollideWorld(World3D *world) { world_ = world; }

void Cloth3D::setColor(float r, float g, float b, float a) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    colorA_ = a;
}

float Cloth3D::getParticleX(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].x;
}

float Cloth3D::getParticleY(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].y;
}

float Cloth3D::getParticleZ(int index) const {
    if (!validIndex(index)) return 0.f;
    return particles_[static_cast<size_t>(index)].z;
}

void Cloth3D::setParticlePosition(int index, float x, float y, float z) {
    if (!validIndex(index)) throw Exception("Cloth3D.setParticlePosition: index out of range");
    Particle &p = particles_[static_cast<size_t>(index)];
    p.x = p.px = x;
    p.y = p.py = y;
    p.z = p.pz = z;
}

int64_t Cloth3D::cellKey(int cx, int cy, int cz) const {
    // 21 bits per axis keeps the packed key inside a signed 64-bit int.
    return int64_t(uint32_t(cx) & 0x1fffffu) |
           (int64_t(uint32_t(cy) & 0x1fffffu) << 21) |
           (int64_t(uint32_t(cz) & 0x1fffffu) << 42);
}

void Cloth3D::rebuildHash() {
    hash_.clear();
    const float cell = std::max(1e-3f, particleSize_ * 2.f);
    const float inv = 1.f / cell;
    for (int i = 0; i < getParticleCount(); ++i) {
        const Particle &p = particles_[static_cast<size_t>(i)];
        const int cx = int(std::floor(p.x * inv));
        const int cy = int(std::floor(p.y * inv));
        const int cz = int(std::floor(p.z * inv));
        hash_[cellKey(cx, cy, cz)].push_back(i);
    }
}

void Cloth3D::integrate(float dt) {
    if (dt <= 0.f) return;
    const float ax = gravityX_ + forceX_;
    const float ay = gravityY_ + forceY_;
    const float az = gravityZ_ + forceZ_;
    const float damp = 1.f - damping_;
    const bool hasInteract = interactRadius_ > 0.f && interactStrength_ != 0.f;

    for (Particle &p : particles_) {
        if (p.pinned) {
            p.px = p.x;
            p.py = p.y;
            p.pz = p.z;
            continue;
        }
        const float vx = (p.x - p.px) * damp;
        const float vy = (p.y - p.py) * damp;
        const float vz = (p.z - p.pz) * damp;
        p.px = p.x;
        p.py = p.y;
        p.pz = p.z;
        p.x += vx + ax * dt * dt;
        p.y += vy + ay * dt * dt;
        p.z += vz + az * dt * dt;
        if (hasInteract) {
            const float dx = interactX_ - p.x;
            const float dy = interactY_ - p.y;
            const float dz = interactZ_ - p.z;
            const float r2 = dx * dx + dy * dy + dz * dz;
            const float R2 = interactRadius_ * interactRadius_;
            if (r2 < R2 && r2 > 1e-6f) {
                const float r = std::sqrt(r2);
                const float w = 1.f - r / interactRadius_;
                const float a = interactStrength_ * w * dt * dt;
                p.x += (dx / r) * a;
                p.y += (dy / r) * a;
                p.z += (dz / r) * a;
            }
        }
        constexpr float maxSpeed = 9.f;  // m/s
        const float maxDisp = maxSpeed * dt;
        const float dvx = p.x - p.px;
        const float dvy = p.y - p.py;
        const float dvz = p.z - p.pz;
        const float v2 = dvx * dvx + dvy * dvy + dvz * dvz;
        if (v2 > maxDisp * maxDisp) {
            const float s = maxDisp / std::sqrt(v2);
            p.px = p.x - dvx * s;
            p.py = p.y - dvy * s;
            p.pz = p.z - dvz * s;
        }
    }
}

void Cloth3D::solveConstraints() {
    for (int iter = 0; iter < iterations_; ++iter) {
        for (const Link &link : links_) {
            Particle &a = particles_[static_cast<size_t>(link.a)];
            Particle &b = particles_[static_cast<size_t>(link.b)];
            if (a.pinned && b.pinned) continue;
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float dz = b.z - a.z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 1e-5f) continue;
            const float diff = (dist - link.rest) / dist * stiffness_;
            if (a.pinned) {
                b.x -= dx * diff;
                b.y -= dy * diff;
                b.z -= dz * diff;
            } else if (b.pinned) {
                a.x += dx * diff;
                a.y += dy * diff;
                a.z += dz * diff;
            } else {
                const float half = diff * 0.5f;
                a.x += dx * half;
                a.y += dy * half;
                a.z += dz * half;
                b.x -= dx * half;
                b.y -= dy * half;
                b.z -= dz * half;
            }
        }
        if (validIndex(grabIndex_)) {
            Particle &g = particles_[static_cast<size_t>(grabIndex_)];
            g.x  = grabX_;
            g.y  = grabY_;
            g.z  = grabZ_;
            g.px = grabX_;
            g.py = grabY_;
            g.pz = grabZ_;
        }
    }
}

void Cloth3D::solveFoldConstraint() {
    if (foldStiffness_ <= 0.f || maxFoldAngle_ >= kPi || foldPairs_.empty()) return;
    // Fold pairs share edge (a,b) with opposite vertices k (tri 1) and l (tri 2).
    // For a flat sheet the in-plane directions m1,m2 point to opposite sides
    // (theta = pi); folding brings them toward the same side (theta -> 0). When
    // theta drops below pi - maxFoldAngle, rotate both triangles rigidly around
    // the shared edge to reopen the fold to the limit.
    const float thetaMin = kPi - maxFoldAngle_;
    const float s = foldStiffness_;
    for (const FoldPair &pair : foldPairs_) {
        Particle &pa = particles_[static_cast<size_t>(pair.a)];
        Particle &pb = particles_[static_cast<size_t>(pair.b)];
        Particle &pk = particles_[static_cast<size_t>(pair.k)];
        Particle &pl = particles_[static_cast<size_t>(pair.l)];
        if (pa.pinned && pb.pinned && pk.pinned && pl.pinned) continue;

        const V3 paV{pa.x, pa.y, pa.z};
        const V3 pbV{pb.x, pb.y, pb.z};
        V3 pkV{pk.x, pk.y, pk.z};
        V3 plV{pl.x, pl.y, pl.z};

        const V3 u = vnorm(sub(pbV, paV));
        const V3 vk = sub(pkV, paV);
        const V3 vl = sub(plV, paV);
        const V3 h1 = sub(vk, mul(dot(vk, u), u));
        const V3 h2 = sub(vl, mul(dot(vl, u), u));
        const float b1 = vlen(h1);
        const float b2 = vlen(h2);
        if (b1 < 1e-6f || b2 < 1e-6f) continue;
        const V3 m1 = mul(1.f / b1, h1);
        const V3 m2 = mul(1.f / b2, h2);
        const float theta = std::acos(std::clamp(dot(m1, m2), -1.f, 1.f));
        if (theta >= thetaMin) continue;

        // Reopen the fold along its own path: a pure fold moves m1/m2 from
        // opposite (flat, theta = pi) toward the same side (theta -> 0) with a
        // fixed rotation sign; rotate both triangles back by delta/2 each.
        const float phi = signedAngle(m1, m2, u);
        const float sign = phi >= 0.f ? 1.f : -1.f;
        const float delta = thetaMin - theta;
        const float a1 = -sign * delta * 0.5f * s;
        const float a2 = sign * delta * 0.5f * s;
        if (!pk.pinned) {
            pkV = add(paV, rotateAround(vk, u, a1 * s));
            pk.x = pkV.x;
            pk.y = pkV.y;
            pk.z = pkV.z;
        }
        if (!pl.pinned) {
            plV = add(paV, rotateAround(vl, u, a2 * s));
            pl.x = plV.x;
            pl.y = plV.y;
            pl.z = plV.z;
        }
    }
}

void Cloth3D::solveSelfCollision() {
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
        const int cz = int(std::floor(pi.z * inv));
        for (int oz = -1; oz <= 1; ++oz) {
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    auto it = hash_.find(cellKey(cx + ox, cy + oy, cz + oz));
                    if (it == hash_.end()) continue;
                    for (int j : it->second) {
                        if (j <= i) continue;
                        if (areLinked(i, j)) continue;
                        Particle &pj = particles_[static_cast<size_t>(j)];
                        float dx = pj.x - pi.x;
                        float dy = pj.y - pi.y;
                        float dz = pj.z - pi.z;
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 >= minDist * minDist || d2 < 1e-8f) continue;
                        if (pi.pinned && pj.pinned) continue;
                        const float d = std::sqrt(d2);
                        const float corr = std::min(0.5f, (minDist - d) / d);
                        float wa = 0.5f;
                        float wb = 0.5f;
                        if (pi.pinned && !pj.pinned) { wa = 0.f; wb = 1.f; }
                        else if (pj.pinned && !pi.pinned) { wa = 1.f; wb = 0.f; }
                        pi.x -= dx * corr * wa;
                        pi.y -= dy * corr * wa;
                        pi.z -= dz * corr * wa;
                        pj.x += dx * corr * wb;
                        pj.y += dy * corr * wb;
                        pj.z += dz * corr * wb;
                    }
                }
            }
        }
    }
}

void Cloth3D::solveSelfCollisionTriangles() {
    if (!selfCollision_ || triangles_.size() < 2) return;
    const float thickness = std::max(1e-3f, particleSize_ * 2.f);
    const int triCount = static_cast<int>(triangles_.size());

    std::vector<V3> pos(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i) {
        pos[i] = {particles_[i].x, particles_[i].y, particles_[i].z};
    }

    const float cell = thickness;
    const float inv = 1.f / cell;
    std::unordered_map<int64_t, std::vector<int>> triHash;
    auto triCellKey = [](int cx, int cy, int cz) -> int64_t {
        return int64_t(uint32_t(cx) & 0x1fffffu) |
               (int64_t(uint32_t(cy) & 0x1fffffu) << 21) |
               (int64_t(uint32_t(cz) & 0x1fffffu) << 42);
    };
    const auto insertTri = [&](int ti) {
        const Tri &tri = triangles_[static_cast<size_t>(ti)];
        V3 mn = pos[static_cast<size_t>(tri.v[0])];
        V3 mx = mn;
        for (int k = 1; k < 3; ++k) {
            const V3 &p = pos[static_cast<size_t>(tri.v[k])];
            mn.x = std::min(mn.x, p.x);
            mn.y = std::min(mn.y, p.y);
            mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x);
            mx.y = std::max(mx.y, p.y);
            mx.z = std::max(mx.z, p.z);
        }
        const V3 pad{thickness, thickness, thickness};
        mn = sub(mn, pad);
        mx = add(mx, pad);
        const int x0 = int(std::floor(mn.x * inv));
        const int y0 = int(std::floor(mn.y * inv));
        const int z0 = int(std::floor(mn.z * inv));
        const int x1 = int(std::floor(mx.x * inv));
        const int y1 = int(std::floor(mx.y * inv));
        const int z1 = int(std::floor(mx.z * inv));
        for (int cz = z0; cz <= z1; ++cz)
            for (int cy = y0; cy <= y1; ++cy)
                for (int cx = x0; cx <= x1; ++cx)
                    triHash[triCellKey(cx, cy, cz)].push_back(ti);
    };
    for (int ti = 0; ti < triCount; ++ti) insertTri(ti);

    const auto sharesVertex = [&](const Tri &a, const Tri &b) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (a.v[i] == b.v[j]) return true;
        return false;
    };

    std::unordered_set<int64_t> processed;
    std::vector<int> cells;
    int featA[3];
    int featB[3];

    for (int i = 0; i < triCount; ++i) {
        const Tri &triI = triangles_[static_cast<size_t>(i)];
        const V3 &pa0 = pos[static_cast<size_t>(triI.v[0])];
        V3 mn = pa0;
        V3 mx = mn;
        for (int k = 1; k < 3; ++k) {
            const V3 &p = pos[static_cast<size_t>(triI.v[k])];
            mn.x = std::min(mn.x, p.x);
            mn.y = std::min(mn.y, p.y);
            mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x);
            mx.y = std::max(mx.y, p.y);
            mx.z = std::max(mx.z, p.z);
        }
        const V3 pad{thickness, thickness, thickness};
        mn = sub(mn, pad);
        mx = add(mx, pad);
        const int x0 = int(std::floor(mn.x * inv));
        const int y0 = int(std::floor(mn.y * inv));
        const int z0 = int(std::floor(mn.z * inv));
        const int x1 = int(std::floor(mx.x * inv));
        const int y1 = int(std::floor(mx.y * inv));
        const int z1 = int(std::floor(mx.z * inv));
        cells.clear();
        for (int cz = z0; cz <= z1; ++cz)
            for (int cy = y0; cy <= y1; ++cy)
                for (int cx = x0; cx <= x1; ++cx) {
                    auto it = triHash.find(triCellKey(cx, cy, cz));
                    if (it != triHash.end()) cells.insert(cells.end(), it->second.begin(),
                                                          it->second.end());
                }
        for (int j : cells) {
            if (j <= i) continue;
            const int64_t key = pairKey(i, j);
            if (!processed.insert(key).second) continue;
            const Tri &triJ = triangles_[static_cast<size_t>(j)];
            if (sharesVertex(triI, triJ)) continue;

            const V3 A[3] = {pos[static_cast<size_t>(triI.v[0])],
                             pos[static_cast<size_t>(triI.v[1])],
                             pos[static_cast<size_t>(triI.v[2])]};
            const V3 B[3] = {pos[static_cast<size_t>(triJ.v[0])],
                             pos[static_cast<size_t>(triJ.v[1])],
                             pos[static_cast<size_t>(triJ.v[2])]};
            V3 cpA;
            V3 cpB;
            int countA = 0;
            int countB = 0;
            const float d = triangleDistance(A, B, featA, featB, countA, countB, cpA, cpB);
            if (d >= thickness) continue;

            V3 n = vnorm(sub(cpB, cpA));
            if (vlen(sub(cpB, cpA)) < 1e-6f) {
                n = vnorm(cross(sub(B[1], B[0]), sub(B[2], B[0])));
            }
            const float corr = (thickness - d) * stiffness_;
            if (corr <= 0.f) continue;

            const auto freeCount = [&](const int *feat, int count) {
                int free = 0;
                for (int k = 0; k < count; ++k)
                    if (!particles_[static_cast<size_t>(feat[k])].pinned) ++free;
                return free;
            };
            const int freeA = freeCount(featA, countA);
            const int freeB = freeCount(featB, countB);
            if (freeA == 0 && freeB == 0) continue;
            const float wA = freeA > 0 ? (freeB > 0 ? 0.5f : 1.f) : 0.f;
            const float wB = freeB > 0 ? (freeA > 0 ? 0.5f : 1.f) : 0.f;
            const auto pushFeature = [&](const int *feat, int count, const V3 &dir) {
                for (int k = 0; k < count; ++k) {
                    Particle &pv = particles_[static_cast<size_t>(feat[k])];
                    if (pv.pinned) continue;
                    pv.x += dir.x;
                    pv.y += dir.y;
                    pv.z += dir.z;
                }
            };
            if (freeA > 0) {
                const V3 dir = mul(-corr * wA / float(freeA), n);
                pushFeature(featA, countA, dir);
            }
            if (freeB > 0) {
                const V3 dir = mul(corr * wB / float(freeB), n);
                pushFeature(featB, countB, dir);
            }
        }
    }

    // Vertex-through-face: push any vertex that has crossed into the prism of a
    // non-adjacent triangle back out along the face normal.
    for (int i = 0; i < triCount; ++i) {
        const Tri &triI = triangles_[static_cast<size_t>(i)];
        const V3 A[3] = {pos[static_cast<size_t>(triI.v[0])],
                         pos[static_cast<size_t>(triI.v[1])],
                         pos[static_cast<size_t>(triI.v[2])]};
        const V3 nI = vnorm(cross(sub(A[1], A[0]), sub(A[2], A[0])));
        for (int j = i + 1; j < triCount; ++j) {
            const Tri &triJ = triangles_[static_cast<size_t>(j)];
            if (sharesVertex(triI, triJ)) continue;
            const V3 B[3] = {pos[static_cast<size_t>(triJ.v[0])],
                             pos[static_cast<size_t>(triJ.v[1])],
                             pos[static_cast<size_t>(triJ.v[2])]};
            const V3 nJ = vnorm(cross(sub(B[1], B[0]), sub(B[2], B[0])));
            // Vertices of tri i against the face of tri j.
            for (int k = 0; k < 3; ++k) {
                Particle &pv = particles_[static_cast<size_t>(triI.v[k])];
                if (pv.pinned) continue;
                const V3 va = A[k];
                const float d = dot(sub(va, B[0]), nJ);
                if (std::fabs(d) >= thickness) continue;
                const V3 q = add(va, mul(-d, nJ));
                if (!pointInTriangle(q, B[0], B[1], B[2])) continue;
                const float push = (thickness - std::fabs(d)) * stiffness_ * 0.5f;
                const V3 dir = mul(d >= 0.f ? push : -push, nJ);
                pv.x += dir.x;
                pv.y += dir.y;
                pv.z += dir.z;
            }
            // Vertices of tri j against the face of tri i.
            for (int k = 0; k < 3; ++k) {
                Particle &pv = particles_[static_cast<size_t>(triJ.v[k])];
                if (pv.pinned) continue;
                const V3 vb = B[k];
                const float d = dot(sub(vb, A[0]), nI);
                if (std::fabs(d) >= thickness) continue;
                const V3 q = add(vb, mul(-d, nI));
                if (!pointInTriangle(q, A[0], A[1], A[2])) continue;
                const float push = (thickness - std::fabs(d)) * stiffness_ * 0.5f;
                const V3 dir = mul(d >= 0.f ? push : -push, nI);
                pv.x += dir.x;
                pv.y += dir.y;
                pv.z += dir.z;
            }
        }
    }
}

void Cloth3D::collideWorld(float dt) {
    if (!world_ || !world_->isValid() || particleSize_ <= 0.f) return;
    const float invDt = dt > 1e-6f ? 1.f / dt : 0.f;
    ClothContact3D contact;
    for (int i = 0; i < getParticleCount(); ++i) {
        Particle &p = particles_[static_cast<size_t>(i)];
        if (p.pinned) continue;
        if (!world_->pointProbe(p.x, p.y, p.z, particleSize_, &contact) || !contact.hit) continue;
        const float vpx = (p.x - p.px) * invDt;
        const float vpy = (p.y - p.py) * invDt;
        const float vpz = (p.z - p.pz) * invDt;
        p.x += contact.nx * contact.depth;
        p.y += contact.ny * contact.depth;
        p.z += contact.nz * contact.depth;

        float vbx = 0.f;
        float vby = 0.f;
        float vbz = 0.f;
        float bodyMass = 0.f;
        const bool dynamic =
            contact.body != nullptr && contact.body->getType() == "dynamic";
        if (dynamic) {
            vbx = contact.body->getLinearVelocityX();
            vby = contact.body->getLinearVelocityY();
            vbz = contact.body->getLinearVelocityZ();
            bodyMass = contact.body->getMass();
        }
        const float vn = (vpx - vbx) * contact.nx + (vpy - vby) * contact.ny +
                         (vpz - vbz) * contact.nz;
        if (vn < 0.f) {
            constexpr float restitution = 0.15f;
            const float m = particleMass_;
            const float reduced = bodyMass > 0.f ? (m * bodyMass) / (m + bodyMass) : m;
            float j = -(1.f + restitution) * vn * reduced;
            const float maxKick = 9.f;  // m/s
            j = std::min(j, maxKick * m);
            const float kick = (j / m) * dt;
            p.x += contact.nx * kick;
            p.y += contact.ny * kick;
            p.z += contact.nz * kick;
            if (dynamic && bodyMass > 0.f) {
                contact.body->applyLinearImpulse(-contact.nx * j, -contact.ny * j,
                                                 -contact.nz * j);
            }
        }
    }
}

void Cloth3D::collideBounds() {
    if (!hasBounds_) return;
    const float minX = boundX_;
    const float minY = boundY_;
    const float minZ = boundZ_;
    const float maxX = boundX_ + boundW_;
    const float maxY = boundY_ + boundH_;
    const float maxZ = boundZ_ + boundD_;
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
        if (p.z < minZ) {
            const float vz = p.z - p.pz;
            p.z  = minZ;
            p.pz = p.z + vz * bounce;
        } else if (p.z > maxZ) {
            const float vz = p.z - p.pz;
            p.z  = maxZ;
            p.pz = p.z + vz * bounce;
        }
    }
}

void Cloth3D::update(float dt) {
    if (destroyed_) return;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;

    const int substeps = 2;
    const float h = dt / float(substeps);
    for (int s = 0; s < substeps; ++s) {
        integrate(h);
        solveConstraints();
        solveFoldConstraint();
        solveSelfCollision();
        solveSelfCollisionTriangles();
        collideWorld(h);
        collideBounds();
        if (grabIndex_ >= 0) {
            Particle &p = particles_[static_cast<size_t>(grabIndex_)];
            p.px = p.x;
            p.py = p.y;
            p.pz = p.z;
        }
    }
    forceX_ = 0.f;
    forceY_ = 0.f;
    forceZ_ = 0.f;
    interactStrength_ = 0.f;
}

void Cloth3D::draw(graphics::Graphics *gfx) {
    if (!gfx || destroyed_ || getParticleCount() < 4) return;

    // Double-sided mesh: front vertices carry the face normals, back vertices
    // duplicate positions with flipped normals and reversed winding.
    const int gridVertices = cols_ * rows_;
    const int vertexCount = gridVertices * 2;
    const int quadCount = (cols_ - 1) * (rows_ - 1);
    const int indexCount = quadCount * 12;

    std::vector<float> pos(static_cast<size_t>(vertexCount) * 3);
    std::vector<float> nrm(static_cast<size_t>(vertexCount) * 3, 0.f);
    std::vector<float> uv(static_cast<size_t>(vertexCount) * 2);
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const size_t i = static_cast<size_t>(r * cols_ + c);
            const Particle &p = particles_[i];
            const float u = cols_ > 1 ? float(c) / float(cols_ - 1) : 0.f;
            const float v = rows_ > 1 ? float(r) / float(rows_ - 1) : 0.f;
            for (int side = 0; side < 2; ++side) {
                const size_t vi = i + static_cast<size_t>(side) * gridVertices;
                pos[vi * 3 + 0] = p.x;
                pos[vi * 3 + 1] = p.y;
                pos[vi * 3 + 2] = p.z;
                uv[vi * 2 + 0] = u;
                uv[vi * 2 + 1] = v;
            }
        }
    }

    // Accumulate face normals.
    for (const Tri &tri : triangles_) {
        const float *pa = &pos[static_cast<size_t>(tri.v[0]) * 3];
        const float *pb = &pos[static_cast<size_t>(tri.v[1]) * 3];
        const float *pc = &pos[static_cast<size_t>(tri.v[2]) * 3];
        float e1x = pb[0] - pa[0], e1y = pb[1] - pa[1], e1z = pb[2] - pa[2];
        float e2x = pc[0] - pa[0], e2y = pc[1] - pa[1], e2z = pc[2] - pa[2];
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            nx /= len; ny /= len; nz /= len;
        } else {
            nx = 0.f; ny = 1.f; nz = 0.f;
        }
        for (int v : tri.v) {
            nrm[static_cast<size_t>(v) * 3 + 0] += nx;
            nrm[static_cast<size_t>(v) * 3 + 1] += ny;
            nrm[static_cast<size_t>(v) * 3 + 2] += nz;
            nrm[(static_cast<size_t>(v) + gridVertices) * 3 + 0] -= nx;
            nrm[(static_cast<size_t>(v) + gridVertices) * 3 + 1] -= ny;
            nrm[(static_cast<size_t>(v) + gridVertices) * 3 + 2] -= nz;
        }
    }
    for (int i = 0; i < gridVertices; ++i) {
        const float nx = nrm[static_cast<size_t>(i) * 3 + 0];
        const float ny = nrm[static_cast<size_t>(i) * 3 + 1];
        const float nz = nrm[static_cast<size_t>(i) * 3 + 2];
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        for (int side = 0; side < 2; ++side) {
            const size_t vi = static_cast<size_t>(i) + static_cast<size_t>(side) * gridVertices;
            const float sign = side == 0 ? 1.f : -1.f;
            if (len > 1e-8f) {
                nrm[vi * 3 + 0] = sign * nx / len;
                nrm[vi * 3 + 1] = sign * ny / len;
                nrm[vi * 3 + 2] = sign * nz / len;
            } else {
                nrm[vi * 3 + 1] = sign;
            }
        }
    }

    if (!mesh_ || meshVertexCount_ != vertexCount || meshIndexCount_ != indexCount) {
        std::vector<uint32_t> indices(static_cast<size_t>(indexCount));
        int out = 0;
        for (int r = 0; r + 1 < rows_; ++r) {
            for (int c = 0; c + 1 < cols_; ++c) {
                const uint32_t a = static_cast<uint32_t>(r * cols_ + c);
                const uint32_t b = static_cast<uint32_t>(r * cols_ + c + 1);
                const uint32_t cc = static_cast<uint32_t>((r + 1) * cols_ + c + 1);
                const uint32_t d = static_cast<uint32_t>((r + 1) * cols_ + c);
                // Front faces (CCW when viewed from +Y).
                indices[static_cast<size_t>(out) + 0] = a;
                indices[static_cast<size_t>(out) + 1] = d;
                indices[static_cast<size_t>(out) + 2] = b;
                indices[static_cast<size_t>(out) + 3] = d;
                indices[static_cast<size_t>(out) + 4] = cc;
                indices[static_cast<size_t>(out) + 5] = b;
                // Back faces (reversed winding on the duplicated vertex set).
                const uint32_t oa = a + static_cast<uint32_t>(gridVertices);
                const uint32_t ob = b + static_cast<uint32_t>(gridVertices);
                const uint32_t occ = cc + static_cast<uint32_t>(gridVertices);
                const uint32_t od = d + static_cast<uint32_t>(gridVertices);
                indices[static_cast<size_t>(out) + 6] = ob;
                indices[static_cast<size_t>(out) + 7] = od;
                indices[static_cast<size_t>(out) + 8] = oa;
                indices[static_cast<size_t>(out) + 9] = ob;
                indices[static_cast<size_t>(out) + 10] = occ;
                indices[static_cast<size_t>(out) + 11] = od;
                out += 12;
            }
        }
        mesh_ = gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), vertexCount,
                                       indices.data(), indexCount);
        meshVertexCount_ = vertexCount;
        meshIndexCount_ = indexCount;
    } else {
        gfx->updateMeshVertices(mesh_, pos.data(), nrm.data(), uv.data(), vertexCount, nullptr, 0);
    }

    if (mesh_) {
        gfx->drawMesh(mesh_, glm::mat4(1.f), nullptr,
                      Color(colorR_, colorG_, colorB_, colorA_));
    }
}

}  // namespace eve::physics
