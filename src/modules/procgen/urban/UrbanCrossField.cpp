#include "procgen/urban/UrbanCrossField.h"

#include "procgen/urban/UrbanGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace eve::procgen::urban {
namespace {

constexpr double kPi = 3.14159265358979323846;

/** @brief Cross-field grid: stores the cross-field angle θ per cell (θ and θ+π/2 are both axes). */
struct CrossField {
    int                 nx      = 0;
    int                 ny      = 0;
    double              ox      = 0.0;  // origin
    double              oy      = 0.0;
    double              spacing = 1.0;
    std::vector<double> phi;  // 2θ in radians, wrapped to [-π, π)
    std::vector<char>   inside;

    bool cellInside(int x, int y) const {
        if (x < 0 || y < 0 || x >= nx || y >= ny) return false;
        return inside[size_t(y) * size_t(nx) + size_t(x)] != 0;
    }

    bool cellInsideClamped(int x, int y, int* cx, int* cy) const {
        const int xx = std::clamp(x, 0, nx - 1);
        const int yy = std::clamp(y, 0, ny - 1);
        if (cx) *cx = xx;
        if (cy) *cy = yy;
        return inside[size_t(yy) * size_t(nx) + size_t(xx)] != 0;
    }

    /** @brief Interpolate the field direction angle θ (radians) at a world point. */
    double thetaAt(const Vec2& p) const {
        const double fx = (p.x - ox) / spacing;
        const double fy = (p.y - oy) / spacing;
        const int    x0 = std::clamp(int(std::floor(fx)), 0, nx - 1);
        const int    y0 = std::clamp(int(std::floor(fy)), 0, ny - 1);
        const int    x1 = std::min(x0 + 1, nx - 1);
        const int    y1 = std::min(y0 + 1, ny - 1);
        const double tx = std::clamp(fx - double(x0), 0.0, 1.0);
        const double ty = std::clamp(fy - double(y0), 0.0, 1.0);
        // Wrapped bilinear interpolation via unit vectors.
        const int ix[2] = {x0, x1};
        const int iy[2] = {y0, y1};
        Vec2      avg{0, 0};
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                const int cx = ix[i];
                const int cy = iy[j];
                double    a  = 0.0;
                if (cellInside(cx, cy)) {
                    a = phi[size_t(cy) * size_t(nx) + size_t(cx)];
                } else {
                    int ccx = cx, ccy = cy;
                    int guard = 0;
                    while (!cellInside(ccx, ccy) && guard++ < 64) {
                        // Cheap fallback: search a small spiral for the nearest inside cell.
                        bool found = false;
                        for (int r = 1; r <= 4; ++r) {
                            for (int dy = -r; dy <= r && !found; ++dy) {
                                for (int dx = -r; dx <= r && !found; ++dx) {
                                    if (cellInside(cx + dx, cy + dy)) {
                                        ccx   = cx + dx;
                                        ccy   = cy + dy;
                                        found = true;
                                    }
                                }
                            }
                            if (found) break;
                        }
                        if (!found) {
                            ccx = cx;
                            ccy = cy;
                            break;
                        }
                    }
                    a = phi[size_t(ccy) * size_t(nx) + size_t(ccx)];
                }
                const double w = (i == 0 ? 1.0 - tx : tx) * (j == 0 ? 1.0 - ty : ty);
                avg            = avg + Vec2{std::cos(a), std::sin(a)} * w;
            }
        }
        double a = std::atan2(avg.y, avg.x);
        if (a < 0) a += 2.0 * kPi;
        return a * 0.5;  // θ = φ/2
    }
};

/** @brief Build the cross field over a grid covering the parcel bbox. */
CrossField buildCrossField(const Polygon& poly, double spacing) {
    double minX = poly[0].x, minY = poly[0].y, maxX = poly[0].x, maxY = poly[0].y;
    for (const Vec2& p : poly) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    const double margin = spacing * 0.5;
    minX -= margin;
    minY -= margin;
    maxX += margin;
    maxY += margin;
    CrossField f;
    f.spacing = spacing;
    f.ox      = minX;
    f.oy      = minY;
    f.nx      = std::max(2, int(std::ceil((maxX - minX) / spacing)));
    f.ny      = std::max(2, int(std::ceil((maxY - minY) / spacing)));
    f.phi.assign(size_t(f.nx) * size_t(f.ny), 0.0);
    f.inside.assign(size_t(f.nx) * size_t(f.ny), 0);

    // Initialize: nearest-boundary tangent angle (θ mod π → φ = 2θ).
    for (int y = 0; y < f.ny; ++y) {
        for (int x = 0; x < f.nx; ++x) {
            const Vec2 c{f.ox + (double(x) + 0.5) * spacing, f.oy + (double(y) + 0.5) * spacing};
            if (!pointInPolygon(c, poly)) continue;
            f.inside[size_t(y) * size_t(f.nx) + size_t(x)] = 1;
            BoundaryPosition pos;
            closestPointOnBoundary(poly, c, &pos);
            // Tangent of the closest boundary edge.
            const size_t n       = poly.size();
            const Vec2&  a       = poly[size_t(pos.edgeIndex)];
            const Vec2&  b       = poly[size_t((pos.edgeIndex + 1) % int(n))];
            const double tangent = std::atan2(b.y - a.y, b.x - a.x);
            double       phi     = std::fmod(2.0 * tangent, 2.0 * kPi);
            if (phi < 0) phi += 2.0 * kPi;
            f.phi[size_t(y) * size_t(f.nx) + size_t(x)] = phi;
        }
    }

    // Smooth the wrapped angle field with Jacobi iterations.
    const int           iterations = 240;
    std::vector<double> next(f.phi.size(), 0.0);
    for (int it = 0; it < iterations; ++it) {
        for (int y = 0; y < f.ny; ++y) {
            for (int x = 0; x < f.nx; ++x) {
                const size_t idx = size_t(y) * size_t(f.nx) + size_t(x);
                if (!f.inside[idx]) continue;
                Vec2      sum{0, 0};
                int       count = 0;
                const int dx[4] = {1, -1, 0, 0};
                const int dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + dx[d];
                    const int ny = y + dy[d];
                    if (!f.cellInside(nx, ny)) continue;
                    const double a = f.phi[size_t(ny) * size_t(f.nx) + size_t(nx)];
                    sum            = sum + Vec2{std::cos(a), std::sin(a)};
                    ++count;
                }
                if (count > 0) {
                    double a = std::atan2(sum.y, sum.x);
                    if (a < 0) a += 2.0 * kPi;
                    next[idx] = a;
                } else {
                    next[idx] = f.phi[idx];
                }
            }
        }
        f.phi.swap(next);
    }
    return f;
}

/**
 * @brief Trace a hyperstreamline from `seed` along the cross-field axis closest to
 * `initialDir`. Stops when it re-hits the boundary; returns the traced polyline.
 */
Polyline traceStreamline(const CrossField& field, const Polygon& poly, const Vec2& seed, const Vec2& initialDir,
                         double spacing, int maxSteps) {
    Polyline out;
    out.push_back(seed);
    Vec2         p           = seed;
    Vec2         d           = normalize(initialDir);
    const double step        = spacing * 0.5;
    const double boundaryTol = spacing * 0.75;
    for (int s = 0; s < maxSteps; ++s) {
        const double theta   = field.thetaAt(p);
        const double c       = std::cos(theta);
        const double sn      = std::sin(theta);
        const Vec2   axes[2] = {{c, sn}, {-sn, c}};
        // Choose the axis (and sign) best aligned with the current direction.
        double bestScore = -1.0;
        Vec2   bestDir   = d;
        for (int i = 0; i < 2; ++i) {
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                const Vec2   cand = axes[i] * double(sgn);
                const double sc   = std::fabs(dot(cand, d));
                if (sc > bestScore) {
                    bestScore = sc;
                    bestDir   = cand;
                }
            }
        }
        d       = bestDir;
        Vec2 np = p + d * step;
        if (!pointInPolygon(np, poly)) {
            // Hit the boundary: project back onto it and finish.
            BoundaryPosition pos;
            const Vec2       hit = closestPointOnBoundary(poly, np, &pos);
            if (distance(hit, out.back()) > 1e-9) out.push_back(hit);
            break;
        }
        // Stop when close to the boundary.
        BoundaryPosition pos;
        const Vec2       bpt = closestPointOnBoundary(poly, np, &pos);
        if (distance(np, bpt) < boundaryTol) {
            if (distance(bpt, out.back()) > 1e-9) out.push_back(bpt);
            break;
        }
        out.push_back(np);
        p = np;
    }
    return out;
}

double polylineChord(const Polyline& pl) {
    if (pl.size() < 2) return 0.0;
    return distance(pl.front(), pl.back());
}

}  // namespace

std::vector<SplitCandidate> generateSplitCandidates(const Polygon& poly, int maxCandidates, double minHalfArea) {
    std::vector<SplitCandidate> out;
    if (poly.size() < 3) return out;
    const double polyArea = area(poly);
    if (polyArea <= 1e-9) return out;

    const double spacing =
        std::clamp(std::sqrt(polyArea) / 20.0, std::sqrt(polyArea) * 0.01, std::sqrt(polyArea) * 0.2);
    const int                         seedCount = std::clamp(int(perimeter(poly) / std::max(1e-6, spacing)), 24, 96);
    const std::vector<BoundarySample> samples   = sampleBoundary(poly, seedCount);
    const CrossField                  field     = buildCrossField(poly, spacing);
    const int                         maxSteps  = std::max(64, int(std::ceil(perimeter(poly) / spacing)));

    // Trace from every boundary sample along each field axis that points into the parcel.
    const Vec2            c = centroid(poly);
    std::vector<Polyline> traces;
    for (const BoundarySample& s : samples) {
        const Vec2   inward  = normalize(c - s.p);
        const double theta   = s.tangentAngle;
        const Vec2   axes[2] = {{std::cos(theta), std::sin(theta)}, {-std::sin(theta), std::cos(theta)}};
        for (const Vec2& axis : axes) {
            if (dot(axis, inward) <= 0.05) continue;  // axis must point into the parcel
            Polyline tr = traceStreamline(field, poly, s.p, axis, spacing, maxSteps);
            if (tr.size() < 4) continue;
            if (polylineChord(tr) < spacing * 2.0) continue;
            traces.push_back(std::move(tr));
        }
    }

    // Validate + dedupe traces, keep the geometrically best ones.
    std::vector<SplitCandidate> valid;
    for (const Polyline& tr : traces) {
        Polygon a, b;
        double  frac = 0.0;
        if (!validSplit(poly, tr, minHalfArea, &a, &b, &frac)) continue;
        SplitCandidate cand;
        cand.line     = tr;
        cand.areaFrac = frac;
        valid.push_back(std::move(cand));
        if (valid.size() >= size_t(maxCandidates) * 3) break;
    }

    // Straight-chord fallback when tracing gives too few usable splits. Sample a
    // spread of boundary-separation distances so the generator's metric can choose
    // among axis-aligned and diagonal candidates.
    if (valid.size() < size_t(maxCandidates)) {
        const int                         k     = std::max(32, seedCount);
        const std::vector<BoundarySample> dense = sampleBoundary(poly, k);
        std::vector<SplitCandidate>       chords;
        // Arc-separation steps, largest first: near-balanced chords (e.g. top-bottom
        // connections of a rectangle) are generated before small corner cuts.
        const int        half = k / 2;
        std::vector<int> steps;
        for (int s = half; s >= 3; --s) steps.push_back(s);
        for (const int step : steps) {
            for (size_t i = 0; i < dense.size(); i += 2) {
                const size_t j = (i + size_t(step)) % dense.size();
                if (j == i) continue;
                Polyline chord{dense[i].p, dense[j].p};
                Polygon  a, b;
                double   frac = 0.0;
                if (!validSplit(poly, chord, minHalfArea, &a, &b, &frac)) continue;
                SplitCandidate cand;
                cand.line     = std::move(chord);
                cand.areaFrac = frac;
                chords.push_back(std::move(cand));
                if (chords.size() >= size_t(maxCandidates) * 3) break;
            }
            if (chords.size() >= size_t(maxCandidates) * 3) break;
        }
        // Append fallbacks (streamlines remain preferred).
        valid.insert(valid.end(), chords.begin(), chords.end());
    }

    // Candidate *selection* is left to the quality metric (paper Eq. 2); here we just
    // keep a diverse set: streamlines first (cross-field), then straight chords, with
    // near-duplicate lines removed. No geometry-based ranking — that would bias the
    // metric (e.g. a rectangle's diagonal is perfectly balanced but should lose to a
    // regular axis-aligned split once regularity is scored).
    const double eps = std::sqrt(area(poly)) * 1e-3;
    for (const SplitCandidate& cand : valid) {
        bool dup = false;
        for (const SplitCandidate& keep : out) {
            const double d1 = distance(cand.line.front(), keep.line.front());
            const double d2 = distance(cand.line.back(), keep.line.back());
            const double d3 = distance(cand.line.front(), keep.line.back());
            const double d4 = distance(cand.line.back(), keep.line.front());
            if ((d1 < eps && d2 < eps) || (d3 < eps && d4 < eps)) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        out.push_back(cand);
        if (int(out.size()) >= maxCandidates) break;
    }
    return out;
}

}  // namespace eve::procgen::urban
