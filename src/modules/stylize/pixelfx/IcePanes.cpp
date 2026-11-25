// eve-hot-path
// Native adaptation of pixelfx ice/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/IcePanes.h"
namespace eve::stylize::pixelfx::ice {
Centroid centroid(std::span<const Point> pts) {
    double a = 0, cx = 0, cy = 0;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const auto   p = pts[j], q = pts[i];
        const double k = p.x * q.y - q.x * p.y;
        a += k;
        cx += (p.x + q.x) * k;
        cy += (p.y + q.y) * k;
    }
    if (std::abs(a) < 1e-6) {
        cx = 0;
        cy = 0;
        for (const auto& p : pts) {
            cx += p.x;
            cy += p.y;
        }
        return {cx / double(pts.size()), cy / double(pts.size()), 0};
    }
    return {cx / (3 * a), cy / (3 * a), std::abs(a) / 2};
}

bool isInsidePoly(std::span<const Point> pts, double x, double y) {
    bool inside = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const auto a = pts[i], b = pts[j];
        if ((a.y > y) != (b.y > y) && x < ((b.x - a.x) * (y - a.y)) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}
std::vector<std::vector<Point>> splitPoly(std::span<const Point> pts, double x, double y, double angle) {
    const double       nx = -std::sin(angle), ny = std::cos(angle);
    const auto         side = [&](Point p) { return (p.x - x) * nx + (p.y - y) * ny; };
    std::vector<Point> L, R;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto   p = pts[i], q = pts[(i + 1) % pts.size()];
        const double sp = side(p), sq = side(q);
        (sp >= 0 ? L : R).push_back(p);
        if ((sp >= 0) != (sq >= 0)) {
            const double u = sp / (sp - sq);
            const Point  m{p.x + (q.x - p.x) * u, p.y + (q.y - p.y) * u};
            L.push_back(m);
            R.push_back(m);
        }
    }
    std::vector<std::vector<Point>> out;
    if (L.size() >= 3) out.push_back(std::move(L));
    if (R.size() >= 3) out.push_back(std::move(R));
    return out;
}
void glassPoly(Paint& p, std::span<const Point> pts, uint32_t color, double alpha) {
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const auto& q : pts) {
        x0 = std::min(x0, q.x);
        x1 = std::max(x1, q.x);
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    for (int y = int(std::floor(y0)); y <= std::ceil(y1); ++y)
        for (int x = int(std::floor(x0)); x <= std::ceil(x1); ++x)
            if (isInsidePoly(pts, x + .5, y + .5)) p.blend(x, y, color, alpha);
}
std::vector<Point> Panes::corners(const Pane& q) const {
    const double       ca = std::cos(q.a), sa = std::sin(q.a), k = std::cos(q.f);
    std::vector<Point> out;
    out.reserve(q.pts.size());
    for (const auto& p : q.pts) {
        const double yy = p.y * k;
        out.push_back({q.x + p.x * ca - yy * sa, q.y + p.x * sa + yy * ca});
    }
    return out;
}
double Panes::seat(const Pane& q) const {
    const double ca = std::cos(q.a), sa = std::sin(q.a), k = std::cos(q.f);
    double       low = -INFINITY;
    for (const auto& p : q.pts) {
        const double yy = p.y * k;
        low             = std::max(low, q.y + p.x * sa + yy * ca);
    }
    return low;
}
void Panes::draw(Paint& p, std::span<const uint32_t> C) const {
    const int  n  = int(C.size());
    const auto at = [&](double i) { return C[size_t(i)]; };
    for (const auto& q : list) {
        const double a = std::min(1., q.life / .35), k = std::cos(q.f), face = std::abs(k),
                     lv    = q.lit * (.35 + .65 * face);
        const auto   pts   = corners(q);
        const bool   flash = face > .97 && !q.rest;
        if (face > .25) {
            if (q.solid && q.tone)
                p.poly(pts, flash ? C[size_t(n - 1)] : at(clamp(jsRound(1 + *q.tone * 5 * (.6 + .4 * face)), 1, n - 2)),
                       a);
            else if (q.solid) {
                const double cr = std::cos(q.ridge), sr = std::sin(q.ridge), ca = std::cos(q.a), sa = std::sin(q.a),
                             rx = cr * ca - sr * k * sa, ry = cr * sa + sr * k * ca;
                double       nx = -ry, ny = rx;
                if (nx * -.55 + ny * -.84 < 0) {
                    nx = -nx;
                    ny = -ny;
                }
                const uint32_t lit  = flash ? C[size_t(n - 1)] : at(clamp(jsRound(3 + lv * 3), 3, n - 2)),
                               deep = at(clamp(jsRound(1 + lv * 2), 1, 3));
                p.poly(
                    pts,
                    [&](double x, double y) {
                        const double d = (x - q.x) * nx + (y - q.y) * ny;
                        return d > .6 ? lit : d > -.6 ? C[size_t(n - 2)] : deep;
                    },
                    a);
            } else
                glassPoly(p, pts, flash ? C[size_t(n - 1)] : at(clamp(jsRound(3 + lv * 3), 3, n - 2)),
                          (.3 + .35 * lv + (flash ? .3 : 0)) * a);
        }
        double cx = 0, cy = 0;
        size_t top = 0;
        for (const auto& v : pts) {
            cx += v.x;
            cy += v.y;
        }
        cx /= double(pts.size());
        cy /= double(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            const auto u = pts[i], v = pts[(i + 1) % pts.size()];
            double     nx = v.y - u.y, ny = u.x - v.x;
            if (nx * ((u.x + v.x) / 2 - cx) + ny * ((u.y + v.y) / 2 - cy) < 0) {
                nx = -nx;
                ny = -ny;
            }
            const double h = jsHypot2(nx, ny), l = h == 0 ? 1 : h, lit = (-nx * .55 - ny * .84) / l;
            p.line(u.x, u.y, v.x, v.y,
                   face < .25 || lit > .35 ? C[size_t(n - 1)]
                   : lit > -.25            ? C[size_t(n - 2)]
                   : q.solid               ? C[1]
                                           : C[size_t(std::max(2, n - 4))],
                   a);
            if (pts[i].y < pts[top].y) top = i;
        }
        if (face > .7) p.dot(pts[top].x, pts[top].y, WHITE, a);
        p.glow(cx, cy, 5, C[size_t(n - 3)], (flash ? .5 : .14) * a);
    }
}
}  // namespace eve::stylize::pixelfx::ice
