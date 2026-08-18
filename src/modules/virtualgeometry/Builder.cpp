#include "virtualgeometry/Builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::virtualgeometry {
namespace {

constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

// ---------------------------------------------------------------------------
// LOD0 meshletization: greedily grow connected clusters from seed triangles
// via a BFS over vertex adjacency, capped by per-cluster vertex/triangle counts.
// ---------------------------------------------------------------------------
struct Meshlet {
    std::vector<std::uint32_t> triangles;  // global corner indices, count % 3 == 0
    std::vector<std::uint32_t> verts;      // unique global vertex indices
};

bool hasVertex(const Meshlet &m, std::uint32_t v) {
    return std::find(m.verts.begin(), m.verts.end(), v) != m.verts.end();
}

bool canAdd(const Meshlet &m, const std::uint32_t corner[3], int maxVerts, int maxTris) {
    if (static_cast<int>(m.triangles.size() / 3) >= maxTris) return false;
    int newVerts = 0;
    for (int k = 0; k < 3; ++k)
        if (!hasVertex(m, corner[k])) ++newVerts;
    return static_cast<int>(m.verts.size()) + newVerts <= maxVerts;
}

void addTriangle(Meshlet &m, const std::uint32_t corner[3]) {
    for (int k = 0; k < 3; ++k)
        if (!hasVertex(m, corner[k])) m.verts.push_back(corner[k]);
    for (int k = 0; k < 3; ++k) m.triangles.push_back(corner[k]);
}

void meshletize(const std::vector<std::uint32_t> &indices, int vertexCount,
                const VirtualGeometryBuilder::Options &opt, std::vector<Meshlet> &out) {
    const int triCount = static_cast<int>(indices.size() / 3);
    if (triCount <= 0) return;

    std::vector<std::vector<int>> vtxTris(static_cast<std::size_t>(vertexCount));
    for (int t = 0; t < triCount; ++t)
        for (int k = 0; k < 3; ++k) vtxTris[indices[3 * t + k]].push_back(t);

    std::vector<char> assigned(static_cast<std::size_t>(triCount), 0);

    for (int seed = 0; seed < triCount; ++seed) {
        if (assigned[seed]) continue;

        Meshlet m;
        std::queue<int> frontier;
        frontier.push(seed);
        const std::uint32_t seedCorner[3] = {indices[3 * seed], indices[3 * seed + 1],
                                             indices[3 * seed + 2]};
        addTriangle(m, seedCorner);
        assigned[seed] = 1;
        for (int k = 0; k < 3; ++k)
            for (int nb : vtxTris[seedCorner[k]])
                if (!assigned[nb]) frontier.push(nb);

        while (!frontier.empty() &&
               static_cast<int>(m.triangles.size() / 3) < opt.maxTrianglesPerCluster) {
            int t = frontier.front();
            frontier.pop();
            if (assigned[t]) continue;  // already added to this cluster (frontier dupes)
            const std::uint32_t corner[3] = {indices[3 * t], indices[3 * t + 1], indices[3 * t + 2]};
            if (!canAdd(m, corner, opt.maxVerticesPerCluster, opt.maxTrianglesPerCluster)) continue;
            addTriangle(m, corner);
            assigned[t] = 1;
            for (int k = 0; k < 3; ++k)
                for (int nb : vtxTris[corner[k]])
                    if (!assigned[nb]) frontier.push(nb);
        }
        out.push_back(std::move(m));
    }
}

void computeSphere(const std::vector<float> &positions, const std::vector<std::uint32_t> &verts,
                   float &cx, float &cy, float &cz, float &r) {
    if (verts.empty()) {
        cx = cy = cz = r = 0.f;
        return;
    }
    double sx = 0, sy = 0, sz = 0;
    for (std::uint32_t v : verts) {
        sx += positions[3 * v];
        sy += positions[3 * v + 1];
        sz += positions[3 * v + 2];
    }
    float n = static_cast<float>(verts.size());
    cx = static_cast<float>(sx) / n;
    cy = static_cast<float>(sy) / n;
    cz = static_cast<float>(sz) / n;
    r = 0.f;
    for (std::uint32_t v : verts) {
        float dx = positions[3 * v] - cx, dy = positions[3 * v + 1] - cy, dz = positions[3 * v + 2] - cz;
        r = std::max(r, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
}

// ---------------------------------------------------------------------------
// QEM (Quadric Error Metric) simplifier. Works on a triangle list in GLOBAL
// vertex indices; collapses edges into an optimal point, avoiding flips.
// Reports the max geometric error radius (sqrt of max quadric cost).
// ---------------------------------------------------------------------------
class QuadricSimplifier {
public:
    QuadricSimplifier(const std::vector<float> &positions,
                      const std::vector<std::uint32_t> &inTris,
                      const std::vector<std::uint32_t> &verts,
                      std::vector<std::uint32_t> &outTris, float &outError)
        : srcPos_(positions), outTris_(outTris), outError_(outError), verts_(verts) {
        tris_ = inTris;
        pos_ = positions;      // mutable working copy for flip tests
        initPos_ = positions;  // original positions for the error metric
    }

    void run(std::size_t targetTriangles) {
        build();
        for (std::uint32_t v : verts_) alive_[v] = 1;

        std::size_t aliveTri = countAliveTriangles();
        double maxCost = 0.0;
        const float kInf = std::numeric_limits<float>::infinity();
        std::unordered_map<std::uint64_t, std::uint8_t> blocked;  // rejected edges

        while (aliveTri > targetTriangles) {
            float bestCost = kInf;
            std::uint32_t bestA = 0, bestB = 0;
            float bestP[3] = {0, 0, 0};
            for (const auto &e : edges_) {
                std::uint32_t a = e.first, b = e.second;
                if (!alive_[a] || !alive_[b]) continue;
                std::uint64_t key = (std::uint64_t(a) << 32) | b;
                if (blocked.count(key)) continue;
                float cost, p[3];
                collapseCost(a, b, cost, p);
                if (cost < bestCost) {
                    bestCost = cost;
                    bestA = a;
                    bestB = b;
                    bestP[0] = p[0];
                    bestP[1] = p[1];
                    bestP[2] = p[2];
                }
            }
            if (bestCost == kInf) break;
            if (!collapse(bestA, bestB, bestP, maxCost)) {
                blocked[(std::uint64_t(bestA) << 32) | bestB] = 1;
                continue;  // skip this edge, try the next-best
            }
            aliveTri = countAliveTriangles();
        }

        outTris_.clear();
        for (std::size_t i = 0; i + 2 < tris_.size(); i += 3) {
            std::uint32_t a = tris_[i], b = tris_[i + 1], c = tris_[i + 2];
            if (a != b && b != c && a != c) {
                outTris_.push_back(a);
                outTris_.push_back(b);
                outTris_.push_back(c);
            }
        }
        outError_ = std::sqrt(maxCost);
    }

private:
    const std::vector<float> &srcPos_;
    std::vector<float> pos_;
    std::vector<std::uint32_t> verts_;
    std::vector<std::uint32_t> tris_;
    std::vector<std::uint32_t> &outTris_;
    float &outError_;
    std::uint32_t maxV_ = 0;
    std::vector<unsigned char> alive_;
    std::vector<double> quadric_;
    std::vector<std::vector<std::size_t>> triIncident_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges_;
    std::vector<float> initPos_;

    // Squared quadric error of vertex v evaluated at its ORIGINAL position.
    double evalQuadricAtInit(std::uint32_t v) const {
        const double *Q = &quadric_[static_cast<std::size_t>(v) * 10];
        double x = initPos_[3 * v], y = initPos_[3 * v + 1], z = initPos_[3 * v + 2];
        return Q[0] * x * x + 2 * Q[1] * x * y + 2 * Q[2] * x * z + 2 * Q[6] * x +
               Q[3] * y * y + 2 * Q[4] * y * z + 2 * Q[7] * y +
               Q[5] * z * z + 2 * Q[8] * z + Q[9];
    }

    void build() {
        maxV_ = 0;
        for (std::uint32_t v : verts_) maxV_ = std::max(maxV_, v);
        alive_.assign(static_cast<std::size_t>(maxV_) + 1, 0);
        triIncident_.assign(static_cast<std::size_t>(maxV_) + 1, {});
        quadric_.assign(static_cast<std::size_t>(maxV_ + 1) * 10, 0.0);
        std::unordered_map<std::uint64_t, std::uint8_t> seen;

        for (std::size_t i = 0; i + 2 < tris_.size(); i += 3) {
            std::uint32_t t[3] = {tris_[i], tris_[i + 1], tris_[i + 2]};
            Vec3 a{pos_[3 * t[0]], pos_[3 * t[0] + 1], pos_[3 * t[0] + 2]};
            Vec3 b{pos_[3 * t[1]], pos_[3 * t[1] + 1], pos_[3 * t[1] + 2]};
            Vec3 c{pos_[3 * t[2]], pos_[3 * t[2] + 1], pos_[3 * t[2] + 2]};
            Vec3 n{(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                   (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                   (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
            float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (nl < 1e-12f) continue;
            n.x /= nl; n.y /= nl; n.z /= nl;
            float d = -(n.x * a.x + n.y * a.y + n.z * a.z);
            double q[10] = {double(n.x) * n.x, double(n.x) * n.y, double(n.x) * n.z,
                            double(n.y) * n.y, double(n.y) * n.z, double(n.z) * n.z,
                            double(n.x) * d,   double(n.y) * d,   double(n.z) * d,
                            double(d) * d};
            for (int k = 0; k < 3; ++k) {
                double *qv = &quadric_[static_cast<std::size_t>(t[k]) * 10];
                for (int j = 0; j < 10; ++j) qv[j] += q[j];
            }
            for (int k = 0; k < 3; ++k) {
                triIncident_[t[k]].push_back(i / 3);
                std::uint32_t u = t[k], v = t[(k + 1) % 3];
                if (u == v) continue;
                std::uint64_t key = (std::uint64_t(std::min(u, v)) << 32) | std::max(u, v);
                if (seen.try_emplace(key, 1).second) edges_.emplace_back(std::min(u, v), std::max(u, v));
            }
        }
    }

    void collapseCost(std::uint32_t a, std::uint32_t b, float &cost, float p[3]) const {
        const double *qa = &quadric_[static_cast<std::size_t>(a) * 10];
        const double *qb = &quadric_[static_cast<std::size_t>(b) * 10];
        double Q[10];
        for (int j = 0; j < 10; ++j) Q[j] = qa[j] + qb[j];
        double A[9] = {Q[0], Q[1], Q[2], Q[1], Q[3], Q[4], Q[2], Q[4], Q[5]};
        double g[3] = {Q[6], Q[7], Q[8]};
        double det = A[0] * (A[4] * A[8] - A[5] * A[7]) -
                     A[1] * (A[3] * A[8] - A[5] * A[6]) +
                     A[2] * (A[3] * A[7] - A[4] * A[6]);
        double x[3];
        if (std::fabs(det) > 1e-12) {
            double inv = 1.0 / det;
            double b1[3] = {-g[0], -g[1], -g[2]};
            x[0] = inv * ((A[4] * A[8] - A[5] * A[7]) * b1[0] +
                          (A[2] * A[7] - A[1] * A[8]) * b1[1] +
                          (A[1] * A[5] - A[2] * A[4]) * b1[2]);
            x[1] = inv * ((A[5] * A[6] - A[3] * A[8]) * b1[0] +
                          (A[0] * A[8] - A[2] * A[6]) * b1[1] +
                          (A[2] * A[3] - A[0] * A[5]) * b1[2]);
            x[2] = inv * ((A[3] * A[7] - A[4] * A[6]) * b1[0] +
                          (A[1] * A[6] - A[0] * A[7]) * b1[1] +
                          (A[0] * A[4] - A[1] * A[3]) * b1[2]);
        } else {
            x[0] = 0.5 * (pos_[3 * a] + pos_[3 * b]);
            x[1] = 0.5 * (pos_[3 * a + 1] + pos_[3 * b + 1]);
            x[2] = 0.5 * (pos_[3 * a + 2] + pos_[3 * b + 2]);
        }
        double cv = Q[0] * x[0] * x[0] + 2 * Q[1] * x[0] * x[1] + 2 * Q[2] * x[0] * x[2] +
                    2 * Q[6] * x[0] + Q[3] * x[1] * x[1] + 2 * Q[4] * x[1] * x[2] +
                    2 * Q[7] * x[1] + Q[5] * x[2] * x[2] + 2 * Q[8] * x[2] + Q[9];
        cost = static_cast<float>(std::max(0.0, cv));
        p[0] = static_cast<float>(x[0]);
        p[1] = static_cast<float>(x[1]);
        p[2] = static_cast<float>(x[2]);
    }

    float signedArea(std::uint32_t a, std::uint32_t b, std::uint32_t c) const {
        Vec3 pa{pos_[3 * a], pos_[3 * a + 1], pos_[3 * a + 2]};
        Vec3 pb{pos_[3 * b], pos_[3 * b + 1], pos_[3 * b + 2]};
        Vec3 pc{pos_[3 * c], pos_[3 * c + 1], pos_[3 * c + 2]};
        Vec3 n{(pb.y - pa.y) * (pc.z - pa.z) - (pb.z - pa.z) * (pc.y - pa.y),
               (pb.z - pa.z) * (pc.x - pa.x) - (pb.x - pa.x) * (pc.z - pa.z),
               (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x)};
        return n.x + n.y + n.z;
    }

    bool flips(std::size_t ti, std::uint32_t a, std::uint32_t b) const {
        std::uint32_t t0 = tris_[ti * 3 + 0], t1 = tris_[ti * 3 + 1], t2 = tris_[ti * 3 + 2];
        const bool usesA = (t0 == a || t1 == a || t2 == a);
        const bool usesB = (t0 == b || t1 == b || t2 == b);
        // Triangles on the collapsed edge become degenerate and are removed;
        // they must not block the collapse.
        if (usesA && usesB) return false;
        std::uint32_t s0 = (t0 == b) ? a : t0, s1 = (t1 == b) ? a : t1, s2 = (t2 == b) ? a : t2;
        if (s0 == s1 || s1 == s2 || s0 == s2) return true;
        float before = signedArea(t0, t1, t2);
        float after = signedArea(s0, s1, s2);
        if (before == 0.f) return true;
        return (after * before) < 0.f;
    }

    bool collapse(std::uint32_t a, std::uint32_t b, const float p[3], double &maxError) {
        float ax = pos_[3 * a], ay = pos_[3 * a + 1], az = pos_[3 * a + 2];
        pos_[3 * a] = p[0]; pos_[3 * a + 1] = p[1]; pos_[3 * a + 2] = p[2];
        bool ok = true;
        for (std::size_t ti : triIncident_[a]) if (flips(ti, a, b)) { ok = false; break; }
        if (ok)
            for (std::size_t ti : triIncident_[b]) if (flips(ti, a, b)) { ok = false; break; }
        if (ok) {
            for (auto &idx : tris_) if (idx == b) idx = a;
            alive_[b] = 0;
            // Merge b's quadric into a, then measure cumulative geometric error
            // (max quadric error over a and b at their original positions).
            double *qa = &quadric_[static_cast<std::size_t>(a) * 10];
            const double *qb = &quadric_[static_cast<std::size_t>(b) * 10];
            for (int j = 0; j < 10; ++j) qa[j] += qb[j];
            maxError = std::max(maxError, std::max(evalQuadricAtInit(a), evalQuadricAtInit(b)));
        } else {
            // Restore original position on rejection.
            pos_[3 * a] = ax; pos_[3 * a + 1] = ay; pos_[3 * a + 2] = az;
        }
        return ok;
    }

    std::size_t countAliveTriangles() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i + 2 < tris_.size(); i += 3) {
            std::uint32_t a = tris_[i], b = tris_[i + 1], c = tris_[i + 2];
            if (a != b && b != c && a != c) ++n;
        }
        return n;
    }
};

}  // namespace

bool VirtualGeometryBuilder::build(const MeshInput &in, const Options &opt,
                                   VirtualGeometryAsset &out) {
    if (in.vertexCount <= 0 || !in.positions || !in.indices || in.indexCount < 3) return false;

    out = VirtualGeometryAsset();
    out.vertexCount = in.vertexCount;
    out.positions.assign(in.positions, in.positions + 3 * static_cast<std::size_t>(in.vertexCount));
    if (in.normals)
        out.normals.assign(in.normals, in.normals + 3 * static_cast<std::size_t>(in.vertexCount));

    std::vector<std::uint32_t> indices(in.indices,
                                       in.indices + static_cast<std::size_t>(in.indexCount));

    // ---- Level 0: meshletize ----
    std::vector<Meshlet> lod0;
    meshletize(indices, in.vertexCount, opt, lod0);
    if (lod0.empty()) return false;

    std::vector<VgCluster> clusters;
    clusters.reserve(lod0.size());
    for (auto &m : lod0) {
        VgCluster c;
        c.triStart = static_cast<std::uint32_t>(out.triangles.size() / 3);  // triangle units
        c.triCount = static_cast<std::uint32_t>(m.triangles.size() / 3);
        c.lodLevel = 0;
        c.errorR = 0.f;
        c.parent = kNoParent;
        c.vertCount = static_cast<std::uint32_t>(m.verts.size());
        computeSphere(out.positions, m.verts, c.cx, c.cy, c.cz, c.r);
        out.triangles.insert(out.triangles.end(), m.triangles.begin(), m.triangles.end());
        clusters.push_back(c);
    }

    // ---- Higher LOD levels: merge + QEM simplify (cluster DAG) ----
    std::vector<VgCluster> current = clusters;
    std::size_t level = 0;
    int totalLevels = 1;
    const std::size_t mergeF = static_cast<std::size_t>(opt.mergeFactor);
    while (current.size() > 1 && totalLevels < opt.minLodLevels) {
        ++level;
        std::vector<VgCluster> parents;
        std::vector<std::vector<std::uint32_t>> simplifiedList;

        for (std::size_t g = 0; g < current.size(); g += mergeF) {
            std::vector<std::uint32_t> unionTris;
            std::vector<std::uint32_t> unionVerts;
            std::vector<unsigned char> seenV(static_cast<std::size_t>(out.vertexCount), 0);
            const std::size_t hi = std::min(current.size(), g + mergeF);
            for (std::size_t k = g; k < hi; ++k) {
                const VgCluster &ch = current[k];
                for (std::uint32_t i = 0; i < ch.triCount * 3; ++i) {
                    std::uint32_t v = out.triangles[static_cast<std::size_t>(ch.triStart) * 3 + i];
                    unionTris.push_back(v);
                    if (!seenV[v]) { seenV[v] = 1; unionVerts.push_back(v); }
                }
            }

            VgCluster parent;
            parent.lodLevel = static_cast<std::uint32_t>(level);
            parent.parent = kNoParent;
            parent.childCount = static_cast<std::uint32_t>(hi - g);

            std::vector<std::uint32_t> simplified;
            float error = 0.f;
            std::size_t target = std::max<std::size_t>(
                1, static_cast<std::size_t>(unionTris.size() / 3 * opt.lodTargetRatio));
            QuadricSimplifier qs(out.positions, unionTris, unionVerts, simplified, error);
            qs.run(target);
            parent.triCount = static_cast<std::uint32_t>(simplified.size() / 3);
            parent.errorR = error;

            std::vector<unsigned char> used(static_cast<std::size_t>(out.vertexCount), 0);
            std::vector<std::uint32_t> pverts;
            for (std::uint32_t v : simplified)
                if (!used[v]) { used[v] = 1; pverts.push_back(v); }
            computeSphere(out.positions, pverts, parent.cx, parent.cy, parent.cz, parent.r);
            parent.vertCount = static_cast<std::uint32_t>(pverts.size());

            parents.push_back(parent);
            simplifiedList.push_back(std::move(simplified));
        }

        // Append simplified triangles; assign real triStart (triangle units).
        for (std::size_t i = 0; i < parents.size(); ++i) {
            parents[i].triStart = static_cast<std::uint32_t>(out.triangles.size() / 3);
            out.triangles.insert(out.triangles.end(), simplifiedList[i].begin(),
                                 simplifiedList[i].end());
        }
        // Wire child <-> parent. `current` are the last current.size() clusters,
        // so global id of current[k] = baseId + k (parents not yet inserted).
        const std::uint32_t baseId = static_cast<std::uint32_t>(clusters.size() - current.size());
        for (std::size_t g = 0; g < current.size(); g += mergeF) {
            std::size_t groupIndex = g / mergeF;
            std::uint32_t pid = static_cast<std::uint32_t>(clusters.size() + groupIndex);
            std::size_t hi = std::min(current.size(), g + mergeF);
            for (std::size_t k = g; k < hi; ++k) {
                current[k].parent = pid;
                parents[groupIndex].children[k - g] = baseId + static_cast<std::uint32_t>(k);
            }
        }
        clusters.insert(clusters.end(), parents.begin(), parents.end());
        current = parents;
        ++totalLevels;
    }

    out.clusters = std::move(clusters);
    return !out.clusters.empty();
}

}  // namespace eve::virtualgeometry
