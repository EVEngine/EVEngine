#include "procgen/urban/UrbanGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace eve::procgen::urban {
namespace {

constexpr double kEps = 1e-9;

int wrapIndex(int i, int n) {
    if (n <= 0) return 0;
    return ((i % n) + n) % n;
}

bool samePoint(const Vec2& a, const Vec2& b, double eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps;
}

bool onSegmentTol(const Vec2& p, const Vec2& a, const Vec2& b, double eps) {
    const Vec2   ab = b - a;
    const double l2 = lengthSq(ab);
    if (l2 <= eps * eps) return samePoint(p, a, eps);
    const double t = dot(p - a, ab) / l2;
    if (t < -eps || t > 1.0 + eps) return false;
    const Vec2 proj = a + ab * t;
    return distance(p, proj) <= eps;
}

}  // namespace

double signedArea(const Polygon& poly) {
    const size_t n = poly.size();
    if (n < 3) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[wrapIndex(int(i) + 1, int(n))];
        s += cross(a, b);
    }
    return s * 0.5;
}

double area(const Polygon& poly) { return std::fabs(signedArea(poly)); }

double polylineLength(const Polyline& pl) {
    double l = 0.0;
    for (size_t i = 1; i < pl.size(); ++i) l += distance(pl[i - 1], pl[i]);
    return l;
}

Vec2 centroid(const Polygon& poly) {
    const size_t n = poly.size();
    if (n == 0) return {0, 0};
    if (n == 1) return poly[0];
    if (n == 2) return (poly[0] + poly[1]) * 0.5;
    double a = 0.0;
    Vec2   c{0, 0};
    for (size_t i = 0; i < n; ++i) {
        const Vec2&  p  = poly[i];
        const Vec2&  q  = poly[wrapIndex(int(i) + 1, int(n))];
        const double cr = cross(p, q);
        a += cr;
        c = c + (p + q) * cr;
    }
    if (std::fabs(a) < 1e-12) return poly[0];
    return c / (3.0 * a);
}

double perimeter(const Polygon& poly) {
    double p = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) p += distance(poly[i], poly[wrapIndex(int(i) + 1, int(poly.size()))]);
    return p;
}

bool ensureCCW(Polygon& poly) {
    if (signedArea(poly) < 0.0) {
        std::reverse(poly.begin(), poly.end());
        return true;
    }
    return false;
}

void cleanupRing(Polygon& poly, double eps) {
    Polygon out;
    out.reserve(poly.size());
    for (const Vec2& p : poly) {
        if (!out.empty() && samePoint(out.back(), p, eps)) continue;
        // Drop points that lie (almost) on the segment between their neighbours.
        if (out.size() >= 2) {
            const Vec2& a = out[out.size() - 2];
            const Vec2& b = out.back();
            if (distanceToSegment(p, a, b) <= eps) {
                out.back() = p;
                continue;
            }
        }
        out.push_back(p);
    }
    // Wrap-around cleanup: first vs last.
    while (out.size() > 3 && samePoint(out.front(), out.back(), eps)) out.pop_back();
    if (out.size() >= 3) {
        const Vec2& a = out[out.size() - 1];
        const Vec2& b = out[0];
        if (distanceToSegment(out[1], a, b) <= eps) out[1] = b;
    }
    poly = std::move(out);
}

bool pointInPolygon(const Vec2& p, const Polygon& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if (pointOnSegment(p, a, b)) return true;
        const bool crosses = ((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

bool pointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b, double eps) { return onSegmentTol(p, a, b, eps); }

bool segmentsIntersect(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, Vec2* out) {
    const Vec2   ab    = b - a;
    const Vec2   cd    = d - c;
    const Vec2   ac    = c - a;
    const double denom = cross(ab, cd);
    const double eps   = 1e-12;
    if (std::fabs(denom) <= eps) return false;  // parallel (collinear handled by point tests)
    const double t = cross(ac, cd) / denom;
    const double u = cross(ac, ab) / denom;
    if (t < -eps || t > 1.0 + eps || u < -eps || u > 1.0 + eps) return false;
    if (out) *out = a + ab * t;
    return true;
}

bool segmentIntersectsPolyline(const Vec2& a, const Vec2& b, const Polyline& pl) {
    for (size_t i = 1; i < pl.size(); ++i) {
        Vec2 hit;
        if (segmentsIntersect(a, b, pl[i - 1], pl[i], &hit)) return true;
    }
    return false;
}

bool polylineSelfIntersects(const Polyline& pl) {
    for (size_t i = 1; i < pl.size(); ++i) {
        for (size_t j = i + 2; j < pl.size(); ++j) {
            Vec2 hit;
            if (segmentsIntersect(pl[i - 1], pl[i], pl[j - 1], pl[j], &hit)) return true;
        }
    }
    return false;
}

bool polygonIsSimple(const Polygon& poly) {
    const size_t n = poly.size();
    if (n < 3) return false;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[wrapIndex(int(i) + 1, int(n))];
        for (size_t j = i + 1; j < n; ++j) {
            const size_t jn = wrapIndex(int(j) + 1, int(n));
            // Adjacent segments share an endpoint and are allowed to touch.
            if (j == i + 1) continue;            // share poly[j]
            if (i == 0 && j == n - 1) continue;  // wrap-around pair shares poly[0]
            Vec2 hit;
            if (segmentsIntersect(a, b, poly[j], poly[jn], &hit)) return false;
        }
    }
    return area(poly) > 1e-12;
}

double closestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b, Vec2* out) {
    const Vec2   ab = b - a;
    const double l2 = lengthSq(ab);
    if (l2 <= 1e-18) {
        if (out) *out = a;
        return distance(p, a);
    }
    const double t = std::clamp(dot(p - a, ab) / l2, 0.0, 1.0);
    if (out) *out = a + ab * t;
    return distance(p, a + ab * t);
}

Vec2 closestPointOnBoundary(const Polygon& poly, const Vec2& p, BoundaryPosition* pos) {
    const size_t n = poly.size();
    if (n == 0) return p;
    double best     = std::numeric_limits<double>::infinity();
    Vec2   bestP    = poly[0];
    int    bestEdge = 0;
    double bestT    = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2&  a  = poly[i];
        const Vec2&  b  = poly[wrapIndex(int(i) + 1, int(n))];
        const Vec2   ab = b - a;
        const double l2 = lengthSq(ab);
        double       t  = 0.0;
        if (l2 > 1e-18) t = std::clamp(dot(p - a, ab) / l2, 0.0, 1.0);
        const Vec2   q = a + ab * t;
        const double d = distance(p, q);
        if (d < best) {
            best     = d;
            bestP    = q;
            bestEdge = int(i);
            bestT    = t;
        }
    }
    if (pos) {
        pos->edgeIndex = bestEdge;
        pos->t         = bestT;
    }
    return bestP;
}

Vec2 pointAtBoundaryLength(const Polygon& poly, double s, BoundaryPosition* pos) {
    const size_t n = poly.size();
    if (n == 0) return {0, 0};
    const double total = perimeter(poly);
    double       t     = std::fmod(s, total);
    if (t < 0) t += total;
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2&  a   = poly[i];
        const Vec2&  b   = poly[wrapIndex(int(i) + 1, int(n))];
        const double seg = distance(a, b);
        if (acc + seg >= t || i + 1 == n) {
            const double f = seg > 1e-12 ? (t - acc) / seg : 0.0;
            if (pos) {
                pos->edgeIndex = int(i);
                pos->t         = std::clamp(f, 0.0, 1.0);
            }
            return a + (b - a) * f;
        }
        acc += seg;
    }
    if (pos) {
        pos->edgeIndex = 0;
        pos->t         = 0.0;
    }
    return poly[0];
}

std::vector<BoundarySample> sampleBoundary(const Polygon& poly, int count) {
    std::vector<BoundarySample> samples;
    const size_t                n = poly.size();
    if (n < 3) return samples;
    const int    k     = std::max(8, count);
    const double total = perimeter(poly);
    samples.reserve(size_t(k));
    for (int i = 0; i < k; ++i) {
        const double     s = double(i) * total / double(k);
        BoundaryPosition pos;
        const Vec2       p   = pointAtBoundaryLength(poly, s, &pos);
        const Vec2&      a   = poly[size_t(pos.edgeIndex)];
        const Vec2&      b   = poly[wrapIndex(pos.edgeIndex + 1, int(n))];
        const Vec2       dir = normalize(b - a);
        samples.push_back({p, std::atan2(dir.y, dir.x), s});
    }
    return samples;
}

double includedAngleDeg(const Vec2& u, const Vec2& v) {
    const Vec2   nu = normalize(u);
    const Vec2   nv = normalize(v);
    const double c  = std::clamp(dot(nu, nv), -1.0, 1.0);
    return std::acos(c) * 180.0 / 3.14159265358979323846;
}

bool isCollinear(const Vec2& prev, const Vec2& shared, const Vec2& next) {
    return includedAngleDeg(prev - shared, next - shared) > 135.0;
}

Polygon approximatePolygon(const Polygon& ring) {
    const size_t n = ring.size();
    if (n <= 3) return ring;
    Polygon out;
    // Group maximal runs of collinear consecutive edges; each run keeps first+last vertex.
    std::vector<char> merged(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const Vec2& prev = ring[wrapIndex(int(i) - 1, int(n))];
        const Vec2& cur  = ring[i];
        const Vec2& next = ring[wrapIndex(int(i) + 1, int(n))];
        if (isCollinear(prev, cur, next)) merged[i] = 1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (merged[i]) continue;
        out.push_back(ring[i]);
    }
    if (out.size() < 3) return ring;
    // Wrap-around: if the ring starts/ends with merged vertices, drop them at the seam.
    while (out.size() > 3) {
        const Vec2& a = out[out.size() - 2];
        const Vec2& b = out.back();
        const Vec2& c = out[0];
        if (isCollinear(a, b, c)) {
            out.pop_back();
            continue;
        }
        const Vec2& d = out[1];
        if (isCollinear(b, out[0], d)) {
            out.erase(out.begin());
            continue;
        }
        break;
    }
    cleanupRing(out);
    return out;
}

double shapeIrregularity(const Polygon& approxRing, double gammaAngle, double gammaSide) {
    const size_t n = approxRing.size();
    if (n < 3) return std::numeric_limits<double>::infinity();
    // Interior angles.
    std::vector<double> angles(n);
    double              angleMean = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2&  prev = approxRing[wrapIndex(int(i) - 1, int(n))];
        const Vec2&  cur  = approxRing[i];
        const Vec2&  next = approxRing[wrapIndex(int(i) + 1, int(n))];
        const Vec2   u    = normalize(prev - cur);
        const Vec2   v    = normalize(next - cur);
        const double c    = std::clamp(dot(u, v), -1.0, 1.0);
        // Interior angle in [0, 2π): the smaller angle may be the exterior one for reflex corners.
        // Use the angle that keeps the polygon inside; for simple CCW polygons the interior
        // angle is the one on the left side of the direction change.
        double a = std::acos(c);
        if (cross(u, v) < 0.0) a = 2.0 * 3.14159265358979323846 - a;
        angles[i] = a;
        angleMean += a;
    }
    angleMean /= double(n);
    double              sideMean = 0.0;
    std::vector<double> sides(n);
    for (size_t i = 0; i < n; ++i) {
        const double l = distance(approxRing[i], approxRing[wrapIndex(int(i) + 1, int(n))]);
        sides[i]       = l;
        sideMean += l;
    }
    sideMean /= double(n);
    double angleVar = 0.0;
    double sideVar  = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double da = angles[i] - angleMean;
        angleVar += da * da;
        const double dl = sides[i] - sideMean;
        sideVar += dl * dl;
    }
    angleVar /= double(n);
    sideVar /= double(n);
    if (sideMean <= 1e-12) return std::numeric_limits<double>::infinity();
    return gammaAngle * angleVar + gammaSide * sideVar / (sideMean * sideMean);
}

namespace {

/** @brief Walk the boundary ring CCW from position `from` to `to`, inserting split points. */
Polygon boundaryChain(const Polygon& poly, const BoundaryPosition& from, const BoundaryPosition& to, const Vec2& pFrom,
                      const Vec2& pTo) {
    const size_t n = poly.size();
    Polygon      chain;
    const int    startEdge = from.edgeIndex;
    const int    endEdge   = to.edgeIndex;
    if (startEdge == endEdge) {
        if (from.t <= to.t) {
            // Both points on the same edge, A before B: the forward chain is the short sub-segment.
            chain.push_back(pFrom);
            if (!(pFrom == pTo)) chain.push_back(pTo);
            return chain;
        }
        // A after B on the same edge: the forward chain wraps all the way around.
        chain.push_back(pFrom);
        int e = startEdge;
        while (true) {
            e = wrapIndex(e + 1, int(n));
            if (e == endEdge) break;
            chain.push_back(poly[size_t(e)]);
        }
        chain.push_back(poly[size_t(endEdge)]);
        chain.push_back(pTo);
        return chain;
    }
    chain.push_back(pFrom);
    int e = wrapIndex(startEdge + 1, int(n));
    while (e != endEdge) {
        chain.push_back(poly[size_t(e)]);
        e = wrapIndex(e + 1, int(n));
    }
    if (to.t > 1e-12) chain.push_back(poly[size_t(endEdge)]);
    chain.push_back(pTo);
    return chain;
}

}  // namespace

bool splitPolygonByPolyline(const Polygon& poly, const Polyline& split, const BoundaryPosition& posA,
                            const BoundaryPosition& posB, Polygon& outA, Polygon& outB) {
    if (poly.size() < 3 || split.size() < 2) return false;
    if (posA.edgeIndex == posB.edgeIndex && std::fabs(posA.t - posB.t) < 1e-9) {
        return false;  // degenerate: same boundary point
    }
    const Vec2 pa = split.front();
    const Vec2 pb = split.back();

    Polygon chainAB = boundaryChain(poly, posA, posB, pa, pb);  // boundary A -> B (CCW)
    Polygon chainBA = boundaryChain(poly, posB, posA, pb, pa);  // boundary B -> A (CCW)

    // Loop 1: chain(A->B) + reversed split (B->A)
    outA = chainAB;
    for (auto it = split.rbegin(); it != split.rend(); ++it) outA.push_back(*it);
    // Loop 2: chain(B->A) + split (A->B)
    outB = chainBA;
    for (const Vec2& p : split) outB.push_back(p);

    cleanupRing(outA);
    cleanupRing(outB);
    ensureCCW(outA);
    ensureCCW(outB);
    return polygonIsSimple(outA) && polygonIsSimple(outB);
}

bool validSplit(const Polygon& poly, const Polyline& split, double minHalfArea, Polygon* outA, Polygon* outB,
                double* fracA) {
    if (split.size() < 2) return false;
    // Endpoints must lie on the boundary.
    BoundaryPosition posA, posB;
    const Vec2       pa      = closestPointOnBoundary(poly, split.front(), &posA);
    const Vec2       pb      = closestPointOnBoundary(poly, split.back(), &posB);
    const double     snapTol = 1e-4 * std::max(1.0, perimeter(poly));
    if (distance(pa, split.front()) > snapTol || distance(pb, split.back()) > snapTol) return false;
    if (distance(pa, pb) <= 1e-9) return false;
    // Every segment midpoint must be strictly inside the polygon (endpoints on the boundary).
    for (size_t i = 1; i < split.size(); ++i) {
        const Vec2 mid = (split[i - 1] + split[i]) * 0.5;
        if (!pointInPolygon(mid, poly)) return false;
    }
    if (polylineSelfIntersects(split)) return false;

    Polygon  a, b;
    Polyline snapped = split;
    snapped.front()  = pa;
    snapped.back()   = pb;
    if (!splitPolygonByPolyline(poly, snapped, posA, posB, a, b)) return false;
    const double areaPoly = area(poly);
    const double areaA    = area(a);
    const double areaB    = area(b);
    if (areaPoly <= 1e-12) return false;
    // Sum-of-areas invariant.
    if (std::fabs(areaA + areaB - areaPoly) > 0.02 * areaPoly) return false;
    if (areaA < minHalfArea || areaB < minHalfArea) return false;
    const double fa = areaA / areaPoly;
    if (fa < 0.02 || fa > 0.98) return false;
    if (outA) *outA = std::move(a);
    if (outB) *outB = std::move(b);
    if (fracA) *fracA = fa;
    return true;
}

bool triangulatePolygon(const Polygon& poly, std::vector<int>& outTriangles) {
    const size_t n = poly.size();
    outTriangles.clear();
    if (n < 3) return false;
    if (!polygonIsSimple(poly)) return false;
    std::vector<int> idx(n);
    for (size_t i = 0; i < n; ++i) idx[size_t(i)] = int(i);
    int guard = 0;
    while (idx.size() > 3 && guard++ < 100000) {
        bool         clipped = false;
        const size_t m       = idx.size();
        for (size_t i = 0; i < m; ++i) {
            const int   iPrev = idx[wrapIndex(int(i) - 1, int(m))];
            const int   iCur  = idx[size_t(i)];
            const int   iNext = idx[wrapIndex(int(i) + 1, int(m))];
            const Vec2& a     = poly[size_t(iPrev)];
            const Vec2& b     = poly[size_t(iCur)];
            const Vec2& c     = poly[size_t(iNext)];
            // Convex ear? For CCW polygon, cross(ab, bc) > 0 means the corner is convex.
            if (cross(b - a, c - b) <= 0.0) continue;
            // No other vertex inside the ear triangle.
            bool empty = true;
            for (size_t j = 0; j < m; ++j) {
                const int v = idx[size_t(j)];
                if (v == iPrev || v == iCur || v == iNext) continue;
                if (pointInPolygon(poly[size_t(v)], {a, b, c})) {
                    empty = false;
                    break;
                }
            }
            if (!empty) continue;
            outTriangles.push_back(iPrev);
            outTriangles.push_back(iCur);
            outTriangles.push_back(iNext);
            idx.erase(idx.begin() + long(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            outTriangles.clear();
            return false;
        }
    }
    if (idx.size() == 3) {
        outTriangles.push_back(idx[0]);
        outTriangles.push_back(idx[1]);
        outTriangles.push_back(idx[2]);
    }
    return outTriangles.size() >= 3;
}

double distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 unused;
    return closestPointOnSegment(p, a, b, &unused);
}

}  // namespace eve::procgen::urban
