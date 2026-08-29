#include "procgen/urban/UrbanGenerator.h"

#include "procgen/urban/UrbanCrossField.h"
#include "procgen/urban/UrbanGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::procgen::urban {
namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

struct UrbanGenerator::GenState {
    std::vector<Polygon>               parcels;  // CCW world-coord rings
    Polygon                            land;
    std::vector<Vec2>                  corners;
    std::vector<Parcel>                rings;  // rings over corners
    std::vector<GraphEdge>             edges;
    std::vector<std::vector<int>>      parcelEdges;  // edge ids per parcel
    std::vector<char>                  edgeStreet;   // mirrors edges[].isStreet
    std::vector<char>                  cornerOnLandBoundary;
    std::vector<std::pair<Vec2, Vec2>> streetSegs;      // street network in world coords
    std::vector<char>                  landEdgeStreet;  // per original land edge
    std::vector<std::vector<int>>      streetChains;    // street polylines as corner indices
    std::vector<int>                   junctionVertices;
};

namespace {

struct EdgeKey {
    int  a = 0;
    int  b = 0;
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const { return (size_t(uint32_t(k.a)) << 32) ^ size_t(uint32_t(k.b)); }
};

EdgeKey makeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return {a, b};
}

/** @brief Weld points into `corners` within `eps` (quantized neighbour search). */
class WeldMap {
public:
    explicit WeldMap(double eps) : eps_(eps) {}

    int add(const Vec2& p, std::vector<Vec2>& corners) {
        const int qx = int(std::floor(p.x / eps_));
        const int qy = int(std::floor(p.y / eps_));
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = buckets_.find(key(qx + dx, qy + dy));
                if (it == buckets_.end()) continue;
                for (const int idx : it->second) {
                    if (distance(p, corners[size_t(idx)]) <= eps_ * 1.5) return idx;
                }
            }
        }
        const int idx = int(corners.size());
        corners.push_back(p);
        buckets_[key(qx, qy)].push_back(idx);
        return idx;
    }

private:
    static uint64_t key(int qx, int qy) { return (uint64_t(uint32_t(qx)) << 32) ^ uint64_t(uint32_t(qy)); }

    double                                         eps_;
    std::unordered_map<uint64_t, std::vector<int>> buckets_;
};

/** @brief Land-boundary street flags, one per original land edge (mode 2 random). */
std::vector<char> rollBoundaryStreetFlags(const Polygon& land, int mode, double fraction, std::mt19937& rng) {
    const size_t      n = land.size();
    std::vector<char> flags(n, 1);
    if (mode == 1) {
        std::fill(flags.begin(), flags.end(), 0);
        return flags;
    }
    if (mode == 2 && n > 0) {
        std::bernoulli_distribution dist(std::clamp(fraction, 0.0, 1.0));
        int                         any = 0;
        for (size_t i = 0; i < n; ++i) {
            flags[i] = dist(rng) ? 1 : 0;
            any += flags[i];
        }
        if (any == 0) flags[0] = 1;  // keep at least one street
    }
    return flags;
}

double landScale(const Polygon& land) {
    if (land.empty()) return 1.0;
    double minX = land[0].x, minY = land[0].y, maxX = land[0].x, maxY = land[0].y;
    for (const Vec2& p : land) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    return std::max(1.0, std::max(maxX - minX, maxY - minY));
}

bool cornerOnLand(const Vec2& p, const Polygon& land, double eps) {
    const size_t n = land.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = land[i];
        const Vec2& b = land[size_t((i + 1) % n)];
        if (distanceToSegment(p, a, b) <= eps) return true;
    }
    return false;
}

/** @brief Match a parcel edge against the street network (containment, so sub-segments inherit). */
bool edgeIsOnStreet(const Vec2& a, const Vec2& b, const std::vector<std::pair<Vec2, Vec2>>& streetSegs, double eps) {
    for (const auto& s : streetSegs) {
        if (distanceToSegment(a, s.first, s.second) <= eps && distanceToSegment(b, s.first, s.second) <= eps)
            return true;
    }
    return false;
}

void rebuildGraph(UrbanGenerator::GenState& st) {
    const double eps = 1e-6 * landScale(st.land);
    st.corners.clear();
    st.rings.clear();
    st.rings.resize(st.parcels.size());
    WeldMap weld(eps);

    for (size_t p = 0; p < st.parcels.size(); ++p) {
        Parcel ring;
        ring.ring.reserve(st.parcels[p].size());
        for (const Vec2& v : st.parcels[p]) ring.ring.push_back(weld.add(v, st.corners));
        st.rings[size_t(p)] = std::move(ring);
    }

    std::unordered_map<EdgeKey, int, EdgeKeyHash> edgeMap;
    st.edges.clear();
    st.parcelEdges.assign(st.parcels.size(), {});
    for (size_t p = 0; p < st.rings.size(); ++p) {
        const std::vector<int>& ring = st.rings[p].ring;
        const size_t            m    = ring.size();
        for (size_t i = 0; i < m; ++i) {
            const EdgeKey k   = makeKey(ring[i], ring[size_t((i + 1) % m)]);
            auto          it  = edgeMap.find(k);
            int           eid = -1;
            if (it == edgeMap.end()) {
                eid = int(st.edges.size());
                st.edges.push_back({k.a, k.b, false});
                edgeMap[k] = eid;
            } else {
                eid = it->second;
            }
            st.parcelEdges[size_t(p)].push_back(eid);
        }
    }

    st.edgeStreet.assign(st.edges.size(), 0);
    for (size_t i = 0; i < st.edges.size(); ++i) {
        const Vec2& a                = st.corners[size_t(st.edges[i].a)];
        const Vec2& b                = st.corners[size_t(st.edges[i].b)];
        st.edgeStreet[size_t(i)]     = edgeIsOnStreet(a, b, st.streetSegs, eps) ? 1 : 0;
        st.edges[size_t(i)].isStreet = st.edgeStreet[size_t(i)] != 0;
    }

    st.cornerOnLandBoundary.assign(st.corners.size(), 0);
    for (size_t i = 0; i < st.corners.size(); ++i) {
        if (cornerOnLand(st.corners[i], st.land, eps)) st.cornerOnLandBoundary[size_t(i)] = 1;
    }
}

struct UnionFind {
    explicit UnionFind(int n) : parent_(size_t(n)) { std::iota(parent_.begin(), parent_.end(), 0); }
    int find(int x) {
        while (parent_[size_t(x)] != x) {
            parent_[size_t(x)] = parent_[size_t(parent_[size_t(x)])];
            x                  = parent_[size_t(x)];
        }
        return x;
    }
    void unite(int a, int b) {
        const int ra = find(a);
        const int rb = find(b);
        if (ra != rb) parent_[size_t(ra)] = rb;
    }

private:
    std::vector<int> parent_;
};

struct AccessPath {
    std::vector<int> vertices;
    std::vector<int> edges;
    double           length  = 0.0;
    int              turns   = 0;
    int              covered = 0;
};

double interiorAngle(const std::vector<Vec2>& corners, const std::vector<int>& ring, size_t i) {
    const size_t m = ring.size();
    if (m < 3) return 0.0;
    const Vec2& prev = corners[size_t(ring[(i + m - 1) % m])];
    const Vec2& cur  = corners[size_t(ring[i])];
    const Vec2& next = corners[size_t(ring[(i + 1) % m])];
    const Vec2  u    = normalize(prev - cur);
    const Vec2  v    = normalize(next - cur);
    double      a    = std::acos(std::clamp(dot(u, v), -1.0, 1.0));
    if (cross(u, v) < 0.0) a = 2.0 * kPi - a;
    return a;
}

}  // namespace

UrbanGenerator::UrbanGenerator(UrbanOptions opts) : opts_(std::move(opts)), rng_(opts_.seed) {}

namespace {

/** @brief Paper Section 5: relax the five-term energy over vertex positions. */
void optimizeLayoutGeometry(UrbanGenerator::GenState& st, const UrbanOptions& opts) {
    const double scale   = landScale(st.land);
    const double eps     = 1e-6 * scale;
    const int    n       = int(st.corners.size());
    const int    parcels = int(st.rings.size());
    double       landA   = 0.0;
    for (const Polygon& poly : st.parcels) landA += area(poly);
    const double            parcelScale = std::sqrt(std::max(1e-9, landA) / double(std::max(1, parcels)));
    const std::vector<Vec2> v0          = st.corners;
    if (n == 0 || parcels == 0) return;

    // Approximate-polygon corners per parcel.
    std::vector<std::vector<int>> approxCorners;
    std::vector<size_t>           approxSides;
    approxCorners.resize(size_t(parcels));
    approxSides.resize(size_t(parcels));
    for (int p = 0; p < parcels; ++p) {
        const std::vector<int>& ring = st.rings[size_t(p)].ring;
        const size_t            m    = ring.size();
        for (size_t i = 0; i < m; ++i) {
            const int prev = ring[(i + m - 1) % m];
            const int cur  = ring[i];
            const int next = ring[(i + 1) % m];
            if (!isCollinear(st.corners[size_t(prev)], st.corners[size_t(cur)], st.corners[size_t(next)])) {
                approxCorners[size_t(p)].push_back(cur);
            }
        }
        approxSides[size_t(p)] = approxCorners[size_t(p)].size();
    }

    // Land-boundary corners: those connecting two collinear boundary edges may slide
    // (paper Eq. 9); all other boundary corners are fixed (paper Eq. 10).
    std::vector<char> fixed(size_t(n), 0);
    std::vector<char> slide(size_t(n), 0);
    {
        std::vector<std::pair<double, int>> boundary;
        for (int v = 0; v < n; ++v) {
            if (!st.cornerOnLandBoundary[size_t(v)]) continue;
            BoundaryPosition pos;
            closestPointOnBoundary(st.land, st.corners[size_t(v)], &pos);
            double s = 0.0;
            for (int e = 0; e < pos.edgeIndex; ++e)
                s += distance(st.land[size_t(e)], st.land[size_t((e + 1) % st.land.size())]);
            s +=
                pos.t * distance(st.land[size_t(pos.edgeIndex)], st.land[size_t((pos.edgeIndex + 1) % st.land.size())]);
            boundary.emplace_back(s, v);
        }
        std::sort(boundary.begin(), boundary.end());
        const size_t m = boundary.size();
        for (size_t i = 0; i < m; ++i) {
            const int v = boundary[i].second;
            if (m == 1) {
                fixed[size_t(v)] = 1;
                continue;
            }
            const int prev = boundary[(i + m - 1) % m].second;
            const int next = boundary[(i + 1) % m].second;
            if (isCollinear(st.corners[size_t(prev)], st.corners[size_t(v)], st.corners[size_t(next)])) {
                slide[size_t(v)] = 1;
            } else {
                fixed[size_t(v)] = 1;
            }
        }
    }

    std::vector<Vec2> moves(size_t(n), {0, 0});
    const double      maxMove = 0.04 * parcelScale;
    for (int iter = 0; iter < std::max(1, opts.optimizeIterations); ++iter) {
        std::fill(moves.begin(), moves.end(), Vec2{0, 0});

        // E_side: Laplacian over collinear parcel side runs.
        for (int p = 0; p < parcels; ++p) {
            const std::vector<int>& ring = st.rings[size_t(p)].ring;
            const size_t            m    = ring.size();
            for (size_t i = 0; i < m; ++i) {
                const int prev = ring[(i + m - 1) % m];
                const int cur  = ring[i];
                const int next = ring[(i + 1) % m];
                if (!isCollinear(st.corners[size_t(prev)], st.corners[size_t(cur)], st.corners[size_t(next)])) continue;
                const Vec2 L = st.corners[size_t(prev)] - st.corners[size_t(cur)] * 2.0 + st.corners[size_t(next)];
                moves[size_t(cur)] = moves[size_t(cur)] + L * opts.optSide;
            }
        }
        // E_stre: Laplacian over street chains (interior vertices).
        for (const std::vector<int>& chain : st.streetChains) {
            for (size_t i = 1; i + 1 < chain.size(); ++i) {
                const Vec2 L            = st.corners[size_t(chain[i - 1])] - st.corners[size_t(chain[i])] * 2.0 +
                                          st.corners[size_t(chain[i + 1])];
                moves[size_t(chain[i])] = moves[size_t(chain[i])] + L * opts.optStre;
            }
        }
        // E_junc: at junctions, push incident street directions toward 90°.
        for (const int jv : st.junctionVertices) {
            std::vector<Vec2> dirs;
            for (const std::vector<int>& chain : st.streetChains) {
                if (chain.size() >= 2 && chain.front() == jv)
                    dirs.push_back(st.corners[size_t(chain[1])] - st.corners[size_t(jv)]);
                if (chain.size() >= 2 && chain.back() == jv)
                    dirs.push_back(st.corners[size_t(chain[chain.size() - 2])] - st.corners[size_t(jv)]);
            }
            for (size_t a = 0; a < dirs.size(); ++a) {
                for (size_t b = a + 1; b < dirs.size(); ++b) {
                    const double la = length(dirs[a]);
                    const double lb = length(dirs[b]);
                    if (la < 1e-9 || lb < 1e-9) continue;
                    const double s = dot(dirs[a], dirs[b]) / (la * lb);
                    // Opposite (collinear) directions are the same street passing through;
                    // only genuine crossings should be pushed toward 90°.
                    if (s < -0.707) continue;
                    const Vec2 pa     = normalize(perpendicular(dirs[b])) * (s * 0.5);
                    const Vec2 pb     = normalize(perpendicular(dirs[a])) * (s * 0.5);
                    moves[size_t(jv)] = moves[size_t(jv)] - (pa + pb) * opts.optJunc * 0.5;
                    for (const auto& chain : st.streetChains) {
                        if (chain.size() < 2) continue;
                        if (chain.front() == jv) moves[size_t(chain[1])] = moves[size_t(chain[1])] + pa * opts.optJunc;
                        if (chain.back() == jv)
                            moves[size_t(chain[chain.size() - 2])] =
                                moves[size_t(chain[chain.size() - 2])] + pb * opts.optJunc;
                    }
                }
            }
        }
        // E_regu: rotate each approximate-polygon corner toward the regular angle
        // (paper Eq. 4; target angle (n-2)π/n of the approximate polygon).
        for (int v = 0; v < n; ++v) {
            if (fixed[size_t(v)]) continue;
            double err = 0.0;
            Vec2   bisector{0, 0};
            for (int p = 0; p < parcels; ++p) {
                const auto&  ring  = st.rings[size_t(p)].ring;
                const size_t sides = approxSides[size_t(p)];
                if (sides < 3) continue;
                for (size_t i = 0; i < ring.size(); ++i) {
                    if (ring[i] != v) continue;
                    bool isCorner = false;
                    for (const int c : approxCorners[size_t(p)])
                        if (c == v) {
                            isCorner = true;
                            break;
                        }
                    if (!isCorner) continue;
                    const double target = (double(sides) - 2.0) * kPi / double(sides);
                    const double theta  = interiorAngle(st.corners, ring, i);
                    const size_t m      = ring.size();
                    const Vec2   prev   = st.corners[size_t(ring[(i + m - 1) % m])];
                    const Vec2   next   = st.corners[size_t(ring[(i + 1) % m])];
                    const Vec2   b =
                        normalize(normalize(prev - st.corners[size_t(v)]) + normalize(next - st.corners[size_t(v)]));
                    if (length(b) > 0.5) {
                        err += std::clamp(target - theta, -0.6, 0.6) * 0.5;
                        bisector = bisector + b;
                    }
                }
            }
            if (std::fabs(err) > 1e-6 && length(bisector) > 0.5) {
                moves[size_t(v)] = moves[size_t(v)] + normalize(bisector) * (err * opts.optRegu * 0.15 * parcelScale);
            }
        }
        // E_close: gentle anchor.
        for (int v = 0; v < n; ++v) {
            moves[size_t(v)] = moves[size_t(v)] + (v0[size_t(v)] - st.corners[size_t(v)]) * (opts.optClose * 0.04);
        }

        std::vector<Vec2> nextPos = st.corners;
        for (int v = 0; v < n; ++v) {
            if (fixed[size_t(v)]) continue;
            Vec2         d  = moves[size_t(v)];
            const double dl = length(d);
            if (dl > maxMove) d = d * (maxMove / dl);
            Vec2 np = st.corners[size_t(v)] + d;
            if (slide[size_t(v)]) {
                BoundaryPosition pos;
                closestPointOnBoundary(st.land, st.corners[size_t(v)], &pos);
                const Vec2& e0 = st.land[size_t(pos.edgeIndex)];
                const Vec2& e1 = st.land[size_t((pos.edgeIndex + 1) % st.land.size())];
                int         n0 = -1, n1 = -1;
                for (int u = 0; u < n; ++u) {
                    if (!st.cornerOnLandBoundary[size_t(u)] || u == v) continue;
                    if (distance(st.corners[size_t(u)], e0) <= eps * 4.0) n0 = u;
                    if (distance(st.corners[size_t(u)], e1) <= eps * 4.0) n1 = u;
                }
                if (n0 >= 0 && n1 >= 0) {
                    const Vec2   dir  = normalize(st.corners[size_t(n1)] - st.corners[size_t(n0)]);
                    const double span = std::max(1e-9, distance(st.corners[size_t(n0)], st.corners[size_t(n1)]));
                    double       t    = dot(np - st.corners[size_t(n0)], dir) / span;
                    t                 = std::clamp(t, 0.01, 0.99);
                    np                = st.corners[size_t(n0)] + dir * (t * span);
                } else {
                    np = st.corners[size_t(v)];
                }
            }
            nextPos[size_t(v)] = np;
        }
        // Validate: every parcel stays simple, CCW, with positive area.
        bool ok = true;
        for (int p = 0; p < parcels; ++p) {
            const std::vector<int>& ring = st.rings[size_t(p)].ring;
            Polygon                 poly;
            poly.reserve(ring.size());
            for (const int c : ring) poly.push_back(nextPos[size_t(c)]);
            if (!polygonIsSimple(poly) || area(poly) < 1e-9 || signedArea(poly) < 0.0) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
        // Moving corners may have collided: verify the welded corner count is unchanged.
        {
            WeldMap           weld(eps);
            std::vector<Vec2> check;
            check.reserve(size_t(n));
            for (const Vec2& c : nextPos) weld.add(c, check);
            if (int(check.size()) != n) break;
        }
        st.corners = nextPos;
    }

    // Write the moved corners back into the world-coord parcel rings.
    for (int p = 0; p < parcels; ++p) {
        const std::vector<int>& ring = st.rings[size_t(p)].ring;
        for (size_t i = 0; i < ring.size(); ++i) st.parcels[size_t(p)][i] = st.corners[size_t(ring[i])];
    }
}

}  // namespace

bool UrbanGenerator::buildBoundaryStreets(GenState& st) {
    st.land = opts_.land;
    if (st.land.size() < 3) return false;
    ensureCCW(st.land);
    cleanupRing(st.land);
    if (st.land.size() < 3) return false;
    st.landEdgeStreet = rollBoundaryStreetFlags(st.land, opts_.boundaryStreetMode, opts_.boundaryStreetFraction, rng_);
    st.streetSegs.clear();
    const size_t n = st.land.size();
    for (size_t i = 0; i < n; ++i) {
        if (st.landEdgeStreet[i]) {
            st.streetSegs.emplace_back(st.land[i], st.land[size_t((i + 1) % n)]);
        }
    }
    return true;
}

bool UrbanGenerator::generate(std::string* error) {
    layout_.clear();
    if (opts_.land.size() < 3) {
        if (error) *error = "urban: land polygon needs at least 3 vertices";
        return false;
    }
    if (opts_.minParcelArea <= 0.0) {
        if (error) *error = "urban: minParcelArea must be positive";
        return false;
    }
    GenState st;
    st.parcels.push_back(opts_.land);
    if (!buildBoundaryStreets(st)) {
        if (error) *error = "urban: invalid land polygon";
        return false;
    }

    rebuildGraph(st);
    int level = 0;
    for (; level < std::max(1, opts_.maxLevels); ++level) {
        std::vector<Polygon> next;
        splitAllParcels(st, next);
        if (next.size() == st.parcels.size()) break;  // nothing splittable
        st.parcels = std::move(next);
        rebuildGraph(st);
        removeShortEdges(st);
        rebuildGraph(st);
        generateStreets(st, level + 1);
        if (opts_.targetParcels > 0 && int(st.parcels.size()) >= opts_.targetParcels) break;
    }
    layout_.levelsUsed = level;

    decomposeStreets(st);
    if (opts_.optimize) {
        // Optimization moves vertices but never changes topology (corner count is
        // validated inside the solver), so street flags transfer by edge index.
        const std::vector<char>    streetFlags = st.edgeStreet;
        const std::vector<Polygon> preParcels  = st.parcels;
        const std::vector<Vec2>    preCorners  = st.corners;
        const auto                 preSegs     = st.streetSegs;
        double                     preIrr      = 0.0;
        for (const Polygon& poly : st.parcels)
            preIrr += shapeIrregularity(approximatePolygon(poly), opts_.gammaAngle, opts_.gammaSide);
        preIrr = st.parcels.empty() ? 0.0 : preIrr / double(st.parcels.size());
        optimizeLayoutGeometry(st, opts_);
        st.edgeStreet = streetFlags;
        for (size_t e = 0; e < st.edges.size(); ++e) st.edges[size_t(e)].isStreet = streetFlags[size_t(e)] != 0;
        st.streetSegs.clear();
        for (size_t e = 0; e < st.edges.size(); ++e) {
            if (streetFlags[size_t(e)]) {
                st.streetSegs.emplace_back(st.corners[size_t(st.edges[e].a)], st.corners[size_t(st.edges[e].b)]);
            }
        }
        decomposeStreets(st);
        double postIrr = 0.0;
        for (const Polygon& poly : st.parcels)
            postIrr += shapeIrregularity(approximatePolygon(poly), opts_.gammaAngle, opts_.gammaSide);
        postIrr = st.parcels.empty() ? 0.0 : postIrr / double(st.parcels.size());
        // The relaxation must not hurt the layout: if irregularity got worse, keep the
        // pre-optimization geometry (the initial layout is already regular).
        if (postIrr > preIrr + 1e-9) {
            st.parcels    = preParcels;
            st.corners    = preCorners;
            st.streetSegs = preSegs;
            st.edgeStreet = streetFlags;
            for (size_t e = 0; e < st.edges.size(); ++e) st.edges[size_t(e)].isStreet = streetFlags[size_t(e)] != 0;
            decomposeStreets(st);
        }
    }
    finalizeStats(st);
    return true;
}

void UrbanGenerator::splitAllParcels(UrbanGenerator::GenState& st, std::vector<Polygon>& nextParcels) {
    nextParcels.clear();
    nextParcels.reserve(st.parcels.size() * 2);
    for (const Polygon& poly : st.parcels) {
        if (opts_.targetParcels > 0 && int(nextParcels.size()) >= opts_.targetParcels) {
            nextParcels.push_back(poly);
            continue;
        }
        Polygon a, b;
        if (splitOneParcel(st, poly, a, b)) {
            nextParcels.push_back(std::move(a));
            nextParcels.push_back(std::move(b));
        } else {
            nextParcels.push_back(poly);
        }
    }
}

bool UrbanGenerator::splitOneParcel(const GenState& st, const Polygon& poly, Polygon& outA, Polygon& outB) const {
    if (area(poly) < opts_.minParcelArea * 2.0) return false;
    std::vector<SplitCandidate> cands = generateSplitCandidates(poly, 20, opts_.minParcelArea);
    if (cands.empty()) return false;

    const double eps         = 1e-6 * landScale(st.land);
    const Vec2   axis        = opts_.orientation == 2   ? Vec2{0.0, 1.0}
                               : opts_.orientation == 1 ? Vec2{1.0, 0.0}
                                                        : Vec2{0.0, 0.0};
    const auto   accessRatio = [&](const Polygon& half) {
        const size_t sides = approximatePolygon(half).size();
        if (sides < 2) return 0.0;
        const double avgSide = perimeter(half) / double(sides);
        if (avgSide <= 1e-9) return 0.0;
        double       streetLen = 0.0;
        const size_t m         = half.size();
        for (size_t i = 0; i < m; ++i) {
            if (edgeIsOnStreet(half[i], half[size_t((i + 1) % m)], st.streetSegs, eps))
                streetLen += distance(half[i], half[size_t((i + 1) % m)]);
        }
        return streetLen / avgSide;
    };

    double  bestScore = -std::numeric_limits<double>::infinity();
    Polygon bestA, bestB;
    for (const SplitCandidate& cand : cands) {
        Polygon a, b;
        double  frac = 0.0;
        if (!validSplit(poly, cand.line, opts_.minParcelArea, &a, &b, &frac)) continue;
        const double qSize   = frac / std::max(1e-9, 1.0 - frac);  // Si/Sj, Si < Sj
        const double ia      = shapeIrregularity(approximatePolygon(a), opts_.gammaAngle, opts_.gammaSide);
        const double ib      = shapeIrregularity(approximatePolygon(b), opts_.gammaAngle, opts_.gammaSide);
        const double qRegu   = 1.0 / (1.0 + ia + ib);
        const double ra      = accessRatio(a);
        const double rb      = accessRatio(b);
        const double qAcce   = (ra >= opts_.accessThreshold && rb >= opts_.accessThreshold) ? 2.0 : 1.0;
        double       qOrient = 0.0;
        if (opts_.orientation != 0) {
            const Vec2 chord = normalize(cand.line.back() - cand.line.front());
            qOrient          = std::fabs(dot(chord, axis));
        }
        const double score = opts_.lambdaSize * qSize + opts_.lambdaRegu * qRegu + opts_.lambdaAcce * qAcce +
                             opts_.lambdaOrient * qOrient;
        if (score > bestScore) {
            bestScore = score;
            bestA     = std::move(a);
            bestB     = std::move(b);
        }
    }
    if (bestScore <= -std::numeric_limits<double>::infinity() / 2.0) return false;
    outA = std::move(bestA);
    outB = std::move(bestB);
    return true;
}

void UrbanGenerator::removeShortEdges(UrbanGenerator::GenState& st) {
    const double eps = 1e-6 * landScale(st.land);
    for (int pass = 0; pass < 24; ++pass) {
        bool         removed = false;
        const size_t ecount  = st.edges.size();
        for (size_t e = 0; e < ecount && !removed; ++e) {
            int p1 = -1, p2 = -1;
            for (size_t p = 0; p < st.parcelEdges.size(); ++p) {
                for (const int pe : st.parcelEdges[size_t(p)]) {
                    if (pe == int(e)) {
                        if (p1 < 0)
                            p1 = int(p);
                        else
                            p2 = int(p);
                    }
                }
            }
            if (p1 < 0 || p2 < 0) continue;
            const Vec2&  a       = st.corners[size_t(st.edges[e].a)];
            const Vec2&  b       = st.corners[size_t(st.edges[e].b)];
            const double len     = distance(a, b);
            const auto   avgSide = [&](int p) {
                const Polygon approx = approximatePolygon(st.parcels[size_t(p)]);
                return approx.empty() ? 0.0 : perimeter(approx) / double(approx.size());
            };
            if (len >= opts_.shortEdgeFactor * std::min(avgSide(p1), avgSide(p2))) continue;
            const Vec2 m = (a + b) * 0.5;
            // Simulate the weld first: every affected parcel must stay a valid polygon
            // (the paper's vertex merge must not collapse a thin neighbouring parcel).
            bool safe = true;
            for (const Polygon& poly : st.parcels) {
                Polygon sim = poly;
                for (Vec2& v : sim) {
                    if (distance(v, a) <= eps || distance(v, b) <= eps) v = m;
                }
                cleanupRing(sim, eps);
                if (sim.size() < 3 || area(sim) < opts_.minParcelArea * 0.5 || !polygonIsSimple(sim)) {
                    safe = false;
                    break;
                }
            }
            if (!safe) continue;
            for (Polygon& poly : st.parcels) {
                for (Vec2& v : poly) {
                    if (distance(v, a) <= eps || distance(v, b) <= eps) v = m;
                }
                cleanupRing(poly, eps);
                ensureCCW(poly);
            }
            removed = true;
        }
        if (!removed) break;
        rebuildGraph(st);
    }
}

void UrbanGenerator::generateStreets(UrbanGenerator::GenState& st, int level) {
    const int n           = int(st.parcels.size());
    auto      reachableOf = [&](int p) {
        for (const int e : st.parcelEdges[size_t(p)])
            if (st.edgeStreet[size_t(e)]) return true;
        return false;
    };
    std::vector<char> reachable(size_t(n), 0);
    for (int p = 0; p < n; ++p) reachable[size_t(p)] = reachableOf(p) ? 1 : 0;

    for (int round = 0; round < 8; ++round) {
        UnionFind                     uf(n);
        std::vector<std::vector<int>> edgeParcels(st.edges.size());
        for (int p = 0; p < n; ++p) {
            if (reachable[size_t(p)]) continue;
            for (const int e : st.parcelEdges[size_t(p)]) edgeParcels[size_t(e)].push_back(p);
        }
        for (size_t e = 0; e < edgeParcels.size(); ++e)
            for (size_t k = 1; k < edgeParcels[e].size(); ++k) uf.unite(edgeParcels[e][0], edgeParcels[e][k]);
        std::vector<std::vector<int>> groups;
        std::vector<int>              groupOf(size_t(n), -1);
        for (int p = 0; p < n; ++p) {
            if (reachable[size_t(p)]) continue;
            const int root = uf.find(p);
            if (groupOf[size_t(root)] < 0) {
                groupOf[size_t(root)] = int(groups.size());
                groups.push_back({});
            }
            groups[size_t(groupOf[size_t(root)])].push_back(p);
        }
        bool changed = false;
        for (const std::vector<int>& group : groups) {
            // Recursively find I/L-shaped street accesses covering every parcel of the
            // group (paper Section 4.2 step 2), then connect the combined access to the
            // existing street network once (step 3).
            std::vector<char> groupEdge(st.edges.size(), 0);
            for (const int p : group)
                for (const int e : st.parcelEdges[size_t(p)]) groupEdge[size_t(e)] = 1;

            std::vector<std::vector<int>> vertexEdges(st.corners.size());
            std::vector<char>             border(st.corners.size(), 0);
            for (size_t e = 0; e < st.edges.size(); ++e) {
                vertexEdges[size_t(st.edges[e].a)].push_back(int(e));
                vertexEdges[size_t(st.edges[e].b)].push_back(int(e));
            }
            for (size_t v = 0; v < st.corners.size(); ++v) {
                if (st.cornerOnLandBoundary[size_t(v)]) {
                    border[size_t(v)] = 1;
                    continue;
                }
                for (const int e : vertexEdges[size_t(v)]) {
                    if (!groupEdge[size_t(e)]) {
                        border[size_t(v)] = 1;
                        break;
                    }
                }
            }

            std::vector<int>  accessEdges;
            std::vector<int>  accessVertices;
            std::vector<char> uncovered(size_t(n), 0);
            for (const int p : group) uncovered[size_t(p)] = 1;
            const int maxLen = 10;
            for (int accessRound = 0; accessRound < 6; ++accessRound) {
                bool anyUncovered = false;
                for (const int p : group)
                    if (uncovered[size_t(p)]) {
                        anyUncovered = true;
                        break;
                    }
                if (!anyUncovered) break;
                // DFS for I/L-shaped paths (<=maxLen edges, <=1 turn) maximizing coverage.
                AccessPath best;
                best.covered = -1;
                std::vector<char> visited(st.corners.size(), 0);
                std::vector<int>  path;
                std::vector<int>  pathEdges;
                std::vector<char> pathCovers(size_t(n), 0);
                int               coveredNow = 0;

                std::function<void(int, int, double, int)> dfs = [&](int v, int depth, double len, int turns) {
                    if (depth > 0 && border[size_t(v)] && turns <= 1) {
                        const double score     = double(coveredNow) * 1e9 - double(turns) * 1e6 - len;
                        const double bestScore = double(best.covered) * 1e9 - double(best.turns) * 1e6 - best.length;
                        if (score > bestScore) {
                            AccessPath cand;
                            cand.vertices = path;
                            cand.edges    = pathEdges;
                            cand.length   = len;
                            cand.turns    = turns;
                            cand.covered  = coveredNow;
                            best          = std::move(cand);
                        }
                    }
                    if (depth >= maxLen) return;
                    // Prefer continuing straight (no new turn) and short edges.
                    std::vector<int> order = vertexEdges[size_t(v)];
                    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                        auto turnCost = [&](int eid) {
                            if (pathEdges.empty()) return 0.0;
                            const int pe = pathEdges.back();
                            const int w = st.edges[size_t(pe)].a == v ? st.edges[size_t(pe)].b : st.edges[size_t(pe)].a;
                            const int nv =
                                st.edges[size_t(eid)].a == v ? st.edges[size_t(eid)].b : st.edges[size_t(eid)].a;
                            return includedAngleDeg(st.corners[size_t(w)] - st.corners[size_t(v)],
                                                    st.corners[size_t(nv)] - st.corners[size_t(v)]);
                        };
                        const double ca = turnCost(a);
                        const double cb = turnCost(b);
                        if (ca != cb) return ca > cb;  // larger included angle = straighter
                        return distance(st.corners[size_t(st.edges[size_t(a)].a)],
                                        st.corners[size_t(st.edges[size_t(a)].b)]) <
                               distance(st.corners[size_t(st.edges[size_t(b)].a)],
                                        st.corners[size_t(st.edges[size_t(b)].b)]);
                    });
                    for (const int eid : order) {
                        if (!groupEdge[size_t(eid)]) continue;
                        if (std::find(pathEdges.begin(), pathEdges.end(), eid) != pathEdges.end()) continue;
                        const int nv = st.edges[size_t(eid)].a == v ? st.edges[size_t(eid)].b : st.edges[size_t(eid)].a;
                        if (visited[size_t(nv)]) continue;
                        const Vec2& pa       = st.corners[size_t(v)];
                        const Vec2& pb       = st.corners[size_t(nv)];
                        double      newLen   = len + distance(pa, pb);
                        int         newTurns = turns;
                        if (!pathEdges.empty()) {
                            const int pe = pathEdges.back();
                            const int w = st.edges[size_t(pe)].a == v ? st.edges[size_t(pe)].b : st.edges[size_t(pe)].a;
                            if (includedAngleDeg(st.corners[size_t(w)] - pa, pb - pa) <= 135.0) ++newTurns;
                        }
                        if (newTurns > 1) continue;
                        visited[size_t(nv)] = 1;
                        path.push_back(nv);
                        pathEdges.push_back(eid);
                        for (const int p : group) {
                            if (uncovered[size_t(p)] && !pathCovers[size_t(p)] &&
                                std::find(st.parcelEdges[size_t(p)].begin(), st.parcelEdges[size_t(p)].end(), eid) !=
                                    st.parcelEdges[size_t(p)].end()) {
                                pathCovers[size_t(p)] = 1;
                                ++coveredNow;
                            }
                        }
                        dfs(nv, depth + 1, newLen, newTurns);
                        coveredNow = 0;
                        std::fill(pathCovers.begin(), pathCovers.end(), 0);
                        for (const int pe : pathEdges) {
                            for (const int p : group) {
                                if (uncovered[size_t(p)] && !pathCovers[size_t(p)] &&
                                    std::find(st.parcelEdges[size_t(p)].begin(), st.parcelEdges[size_t(p)].end(), pe) !=
                                        st.parcelEdges[size_t(p)].end()) {
                                    pathCovers[size_t(p)] = 1;
                                    ++coveredNow;
                                }
                            }
                        }
                        path.pop_back();
                        pathEdges.pop_back();
                        visited[size_t(nv)] = 0;
                    }
                };

                for (size_t v = 0; v < st.corners.size(); ++v) {
                    if (!border[size_t(v)]) continue;
                    visited[size_t(v)] = 1;
                    path.push_back(int(v));
                    dfs(int(v), 0, 0.0, 0);
                    path.pop_back();
                    visited[size_t(v)] = 0;
                }
                if (best.edges.empty()) break;

                for (const int e : best.edges) accessEdges.push_back(e);
                for (const int v : best.vertices) accessVertices.push_back(v);
                // Mark covered parcels done so the next access targets the remainder.
                for (const int pe : best.edges) {
                    for (int p = 0; p < n; ++p) {
                        if (uncovered[size_t(p)] &&
                            std::find(st.parcelEdges[size_t(p)].begin(), st.parcelEdges[size_t(p)].end(), pe) !=
                                st.parcelEdges[size_t(p)].end()) {
                            uncovered[size_t(p)] = 0;
                        }
                    }
                }
            }

            for (const int e : accessEdges) {
                if (!st.edgeStreet[size_t(e)]) {
                    st.edgeStreet[size_t(e)]     = 1;
                    st.edges[size_t(e)].isStreet = true;
                    st.streetSegs.emplace_back(st.corners[size_t(st.edges[e].a)], st.corners[size_t(st.edges[e].b)]);
                }
            }
            if (!accessVertices.empty()) connectAccessToNetwork(st, accessVertices);
            changed = true;
        }
        if (!changed) break;
        for (int p = 0; p < n; ++p) reachable[size_t(p)] = reachableOf(p) ? 1 : 0;
    }
    reconnectStreetEnds(st, level);
}

void UrbanGenerator::connectAccessToNetwork(UrbanGenerator::GenState& st, const std::vector<int>& accessVertices,
                                            const std::vector<char>* excludeTargets) {
    if (accessVertices.empty()) return;
    std::vector<char> isAccess(st.corners.size(), 0);
    for (const int v : accessVertices) isAccess[size_t(v)] = 1;
    std::vector<char> target(st.corners.size(), 0);
    int               targetCount = 0;
    for (size_t e = 0; e < st.edges.size(); ++e) {
        if (!st.edgeStreet[e]) continue;
        for (const int v : {st.edges[e].a, st.edges[e].b}) {
            if (!isAccess[size_t(v)] && !target[size_t(v)] && (!excludeTargets || !(*excludeTargets)[size_t(v)])) {
                target[size_t(v)] = 1;
                ++targetCount;
            }
        }
    }
    if (targetCount == 0) return;

    struct State {
        int    v    = -1;
        int    pe   = -1;
        double cost = 0.0;
    };
    struct Cmp {
        bool operator()(const State& a, const State& b) const { return a.cost > b.cost; }
    };
    std::priority_queue<State, std::vector<State>, Cmp> pq;
    std::unordered_map<uint64_t, double>                best;
    std::unordered_map<uint64_t, uint64_t>              parent;
    auto stateId = [&](int v, int pe) { return (uint64_t(uint32_t(v)) << 32) ^ uint64_t(uint32_t(pe + 1)); };
    std::unordered_set<uint64_t> startStates;

    std::vector<std::vector<int>> vertexEdges(st.corners.size());
    for (size_t e = 0; e < st.edges.size(); ++e) {
        vertexEdges[size_t(st.edges[e].a)].push_back(int(e));
        vertexEdges[size_t(st.edges[e].b)].push_back(int(e));
    }
    for (const int v : accessVertices) {
        for (const int e : vertexEdges[size_t(v)]) {
            const uint64_t id = stateId(v, e);
            best[id]          = 0.0;
            startStates.insert(id);
            pq.push({v, e, 0.0});
        }
    }

    State goal{-1, -1, std::numeric_limits<double>::infinity()};
    while (!pq.empty()) {
        const State s = pq.top();
        pq.pop();
        const uint64_t sid = stateId(s.v, s.pe);
        auto           it  = best.find(sid);
        if (it == best.end() || s.cost > it->second + 1e-12) continue;
        if (target[size_t(s.v)] && startStates.count(stateId(s.v, s.pe)) == 0) {
            goal = s;
            break;
        }
        for (const int e : vertexEdges[size_t(s.v)]) {
            if (e == s.pe) continue;
            const int      nv = st.edges[size_t(e)].a == s.v ? st.edges[size_t(e)].b : st.edges[size_t(e)].a;
            const Vec2&    u  = st.corners[size_t(s.v)];
            const int      w  = st.edges[size_t(s.pe)].a == s.v ? st.edges[size_t(s.pe)].b : st.edges[size_t(s.pe)].a;
            const Vec2&    v  = st.corners[size_t(nv)];
            const double   angle    = includedAngleDeg(st.corners[size_t(w)] - u, v - u);
            const double   turnCost = angle <= 135.0 ? opts_.dijkstraJunctionWeight : 0.0;
            const double   nc       = s.cost + distance(u, v) + turnCost;
            const uint64_t nid      = stateId(nv, e);
            auto           nit      = best.find(nid);
            if (nit == best.end() || nc < nit->second - 1e-12) {
                best[nid]   = nc;
                parent[nid] = sid;
                pq.push({nv, e, nc});
            }
        }
    }
    if (goal.v < 0) return;

    std::vector<int> edgesOut;
    {
        int v  = goal.v;
        int pe = goal.pe;
        while (pe >= 0) {
            edgesOut.push_back(pe);
            const uint64_t id = stateId(v, pe);
            if (startStates.count(id) != 0) break;
            auto pit = parent.find(id);
            if (pit == parent.end()) break;
            const uint64_t nid = pit->second;
            v                  = int(nid >> 32);
            pe                 = int(uint32_t(nid)) - 1;
        }
        std::reverse(edgesOut.begin(), edgesOut.end());
    }
    for (const int e : edgesOut) {
        if (!st.edgeStreet[size_t(e)]) {
            st.edgeStreet[size_t(e)]     = 1;
            st.edges[size_t(e)].isStreet = true;
            st.streetSegs.emplace_back(st.corners[size_t(st.edges[e].a)], st.corners[size_t(st.edges[e].b)]);
        }
    }
}

void UrbanGenerator::reconnectStreetEnds(UrbanGenerator::GenState& st, int level) {
    if (opts_.streetPattern == 3) return;  // tree: keep cul-de-sacs
    if (opts_.streetPattern == 2 && level >= opts_.culDeSacAfterLevel) return;
    for (int round = 0; round < 8; ++round) {
        std::vector<int> ends;
        std::vector<int> streetDegree(st.corners.size(), 0);
        for (size_t e = 0; e < st.edges.size(); ++e) {
            if (!st.edgeStreet[e]) continue;
            ++streetDegree[size_t(st.edges[e].a)];
            ++streetDegree[size_t(st.edges[e].b)];
        }
        for (size_t v = 0; v < st.corners.size(); ++v)
            if (streetDegree[size_t(v)] == 1) ends.push_back(int(v));
        if (ends.empty()) break;
        bool connectedAny = false;
        for (const int v : ends) {
            std::vector<char> exclude(st.corners.size(), 0);
            exclude[size_t(v)] = 1;
            for (size_t e = 0; e < st.edges.size(); ++e) {
                if (!st.edgeStreet[e]) continue;
                if (st.edges[e].a == v) exclude[size_t(st.edges[e].b)] = 1;
                if (st.edges[e].b == v) exclude[size_t(st.edges[e].a)] = 1;
            }
            const size_t before = st.streetSegs.size();
            connectAccessToNetwork(st, {v}, &exclude);
            if (st.streetSegs.size() != before) connectedAny = true;
        }
        if (!connectedAny) break;
    }
}

void UrbanGenerator::decomposeStreets(UrbanGenerator::GenState& st) {
    std::vector<std::vector<int>> adj(st.corners.size());
    std::vector<char>             used(st.edges.size(), 0);
    for (size_t e = 0; e < st.edges.size(); ++e) {
        if (!st.edgeStreet[e]) continue;
        adj[size_t(st.edges[e].a)].push_back(int(e));
        adj[size_t(st.edges[e].b)].push_back(int(e));
    }
    auto isJunctionOrEnd = [&](int v) {
        if (adj[size_t(v)].size() != 2) return true;
        const int e1 = adj[size_t(v)][0];
        const int e2 = adj[size_t(v)][1];
        const int w1 = st.edges[size_t(e1)].a == v ? st.edges[size_t(e1)].b : st.edges[size_t(e1)].a;
        const int w2 = st.edges[size_t(e2)].a == v ? st.edges[size_t(e2)].b : st.edges[size_t(e2)].a;
        return includedAngleDeg(st.corners[size_t(w1)] - st.corners[size_t(v)],
                                st.corners[size_t(w2)] - st.corners[size_t(v)]) <= 135.0;
    };

    layout_.streets.clear();
    st.streetChains.clear();
    st.junctionVertices.clear();
    int junctions = 0;
    int ends      = 0;
    for (size_t v = 0; v < st.corners.size(); ++v) {
        if (adj[size_t(v)].empty()) continue;
        if (adj[size_t(v)].size() == 1)
            ++ends;
        else if (isJunctionOrEnd(int(v)))
            ++junctions;
    }

    auto walk = [&](int startV, int firstE, bool allowLoop) {
        (void)allowLoop;
        std::vector<int> chain;
        chain.push_back(startV);
        int v     = startV;
        int e     = firstE;
        int guard = 0;
        while (guard++ < 100000) {
            used[size_t(e)] = 1;
            const int nv    = st.edges[size_t(e)].a == v ? st.edges[size_t(e)].b : st.edges[size_t(e)].a;
            chain.push_back(nv);
            v = nv;
            if (isJunctionOrEnd(v)) break;
            const int nextE = adj[size_t(v)][0] == e ? adj[size_t(v)][1] : adj[size_t(v)][0];
            if (used[size_t(nextE)]) break;
            e = nextE;
        }
        if (chain.size() >= 2) {
            st.streetChains.push_back(chain);
            const int f = chain.front();
            const int b = chain.back();
            if (isJunctionOrEnd(f) && adj[size_t(f)].size() != 1 &&
                std::find(st.junctionVertices.begin(), st.junctionVertices.end(), f) == st.junctionVertices.end())
                st.junctionVertices.push_back(f);
            if (isJunctionOrEnd(b) && adj[size_t(b)].size() != 1 &&
                std::find(st.junctionVertices.begin(), st.junctionVertices.end(), b) == st.junctionVertices.end())
                st.junctionVertices.push_back(b);
        }
    };

    for (size_t v = 0; v < st.corners.size(); ++v) {
        if (isJunctionOrEnd(int(v))) {
            for (const int e : adj[size_t(v)])
                if (!used[size_t(e)]) walk(int(v), e, false);
        }
    }
    for (size_t e = 0; e < st.edges.size(); ++e) {
        if (st.edgeStreet[e] && !used[e]) walk(st.edges[e].a, int(e), true);
    }
    layout_.streetJunctions = junctions;
    layout_.streetEnds      = ends;

    for (const std::vector<int>& chain : st.streetChains) {
        Street s;
        s.width = opts_.streetWidth;
        for (const int c : chain) s.pts.push_back(st.corners[size_t(c)]);
        layout_.streets.push_back(std::move(s));
    }
}

void UrbanGenerator::finalizeStats(UrbanGenerator::GenState& st) {
    layout_.corners = st.corners;
    layout_.parcels = st.rings;
    layout_.edges   = st.edges;
    layout_.streetSegments.clear();
    for (size_t e = 0; e < st.edges.size(); ++e)
        if (st.edgeStreet[e]) layout_.streetSegments.emplace_back(st.edges[e].a, st.edges[e].b);
    layout_.totalStreetLength = 0.0;
    for (const Street& s : layout_.streets) layout_.totalStreetLength += polylineLength(s.pts);

    double sum = 0.0;
    double mn  = std::numeric_limits<double>::infinity();
    double mx  = 0.0;
    for (const Polygon& poly : st.parcels) {
        const double irr = shapeIrregularity(approximatePolygon(poly), opts_.gammaAngle, opts_.gammaSide);
        sum += irr;
        mn = std::min(mn, irr);
        mx = std::max(mx, irr);
    }
    layout_.avgIrregularity = st.parcels.empty() ? 0.0 : sum / double(st.parcels.size());
    layout_.minIrregularity = st.parcels.empty() ? 0.0 : mn;
    layout_.maxIrregularity = st.parcels.empty() ? 0.0 : mx;
}

}  // namespace eve::procgen::urban
