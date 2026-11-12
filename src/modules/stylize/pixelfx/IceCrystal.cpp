// eve-hot-path
// Native adaptation of pixelfx ice/ice-crystal.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr double TIP = .28;
struct Vec {
    double x, y, z;
};
Vec norm(Vec q) {
    const double l = jsHypot3(q.x, q.y, q.z);
    const double d = l == 0 ? 1 : l;
    return {q.x / d, q.y / d, q.z / d};
}
struct Face {
    std::vector<Point> pts;
    double             lit, spec, cx, cy;
};
/** @brief Build call-owned visible facets; no callbacks or retained inputs.
 * @cost Allocates at most 18 small facets per draw, bounded by the six-sided prism. */
std::vector<Face> faces(double th) {
    constexpr double   R  = 8;
    const double       ct = std::cos(TIP), st = std::sin(TIP);
    const auto         P = [&](double x, double y, double z) -> Vec { return {x, y * ct - z * st, y * st + z * ct}; };
    std::array<Vec, 6> top{}, bot{};
    for (int k = 0; k < 6; ++k) {
        const double a = th + (k * TAU) / 6;
        top[size_t(k)] = P(std::cos(a) * R, -R * .45, std::sin(a) * R);
        bot[size_t(k)] = P(std::cos(a) * R, R * .45, std::sin(a) * R);
    }
    const Vec A = P(0, -R * 2.2, 0), Z = P(0, R * 1.7, 0), L = norm({-.55, -.75, -.45}), h = norm({L.x, L.y, L.z - 1});
    std::vector<Face> out;
    out.reserve(18);
    const auto append = [&](std::initializer_list<Vec> vertices) {
        const auto* f = vertices.begin();
        const Vec a = f[0], b = f[1], c = f[2], u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
        Vec       n  = norm({u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x});
        double    cx = 0, cy = 0, cz = 0;
        for (const auto& q : vertices) {
            cx += q.x;
            cy += q.y;
            cz += q.z;
        }
        const double count = double(vertices.size());
        cx /= count;
        cy /= count;
        cz /= count;
        if (n.x * cx + n.y * cy + n.z * cz < 0) n = {-n.x, -n.y, -n.z};
        if (n.z >= -.02) return;
        Face face{{},
                  std::max(0., n.x * L.x + n.y * L.y + n.z * L.z),
                  std::max(0., n.x * h.x + n.y * h.y + n.z * h.z),
                  cx,
                  cy};
        face.pts.reserve(vertices.size());
        for (const auto& q : vertices) face.pts.push_back({q.x, q.y});
        out.push_back(std::move(face));
    };
    for (size_t k = 0; k < 6; ++k) {
        const size_t j = (k + 1) % 6;
        append({A, top[j], top[k]});
        append({top[k], top[j], bot[j], bot[k]});
        append({bot[k], bot[j], Z});
    }
    return out;
}
/** @brief Build a call-owned silhouette from borrowed facets synchronously.
 * @cost Sorts at most 60 projected corners per draw; no cache or external state. */
std::vector<Point> hullOf(const std::vector<Face>& fs) {
    std::vector<Point> points, lo, hi;
    for (const auto& f : fs)
        for (const auto& p : f.pts) points.push_back(p);
    std::stable_sort(points.begin(), points.end(),
                     [](Point a, Point b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    const auto cross = [](Point o, Point a, Point b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    for (const auto& q : points) {
        while (lo.size() >= 2 && cross(lo[lo.size() - 2], lo.back(), q) <= 0) lo.pop_back();
        lo.push_back(q);
    }
    for (auto i = points.rbegin(); i != points.rend(); ++i) {
        while (hi.size() >= 2 && cross(hi[hi.size() - 2], hi.back(), *i) <= 0) hi.pop_back();
        hi.push_back(*i);
    }
    lo.pop_back();
    hi.pop_back();
    lo.insert(lo.end(), hi.begin(), hi.end());
    return lo;
}
void motes(Paint& p, double t, const PixelFxSettings& o, double Y, bool front) {
    const auto& C = o.palette;
    cycle(t, 3, 16, o.seed, 1.6, 2.6, [&](double age, double life, Rng& r, int i) {
        const double angle = r.range(0, TAU), speed = r.range(1.6, 2.6), R = r.range(14, 22), baseY = r.range(-14, 12),
                     rise = r.range(3, 7), th = angle + age * speed, y = Y + baseY - age * rise, s = std::sin(th),
                     k = age / life;
        if ((s >= 0) != front) return;
        const double x = std::cos(th) * R, yy = y + s * R * .3,
                     a = std::min(1., age / .2) * (k > .75 ? (1 - k) / .25 : 1);
        if (front) {
            p.dot(x, yy, i % 4 == 0 ? WHITE : C[6], a);
            if (i % 4 == 0)
                for (const Point d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                    p.dot(x + d.x, yy + d.y, C[5], a);
            p.glow(x, yy, 4, C[5], .3 * a);
        } else
            p.dot(x, yy, C[4], a * .9);
    });
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / 3, bob = std::sin(TAU * ph) * 2.5, Y = -o.ice.height + bob, s = 1 - bob / 12, rx = 11 * s;
    for (int x = int(std::floor(-rx)); x <= rx; ++x) {
        const double f = 1 - (x / rx) * (x / rx);
        p.blend(x + .5, 0, C[0], .65 * f);
        if (std::abs(x) < rx * .6) p.blend(x + .5, 1, C[0], .4 * f);
    }
    p.glow(0, -1, 24, C[4], .2 + .05 * std::sin(TAU * ph * 2));
    motes(p, t, o, Y, false);
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / 3, bob = std::sin(TAU * ph) * 2.5, Y = -o.ice.height + bob;
    p.glow(0, Y, 34, C[3], .26);
    p.glow(0, Y, 14, C[5], .18 + .08 * std::sin(TAU * ph * 3));
    const auto fs   = faces((TAU / 6) * ph);
    auto       hull = hullOf(fs);
    double     mx = 0, my = 0;
    for (const auto& q : hull) {
        mx += q.x;
        my += q.y;
    }
    mx /= double(hull.size());
    my /= double(hull.size());
    for (auto& q : hull) {
        const double l = jsHypot2(q.x - mx, q.y - my), d = l == 0 ? 1 : l;
        q = {q.x + ((q.x - mx) / d) * 1.2, q.y + Y + ((q.y - my) / d) * 1.2};
    }
    p.poly(hull, C[1]);
    for (const auto& f : fs) {
        const auto c   = f.spec > .985 ? WHITE : C[size_t(clamp(jsRound(1 + std::pow(f.lit, 1.4) * 6.6), 1, 7))];
        auto       pts = f.pts;
        for (auto& q : pts) q.y += Y;
        p.poly(pts, c);
    }
    for (const auto& f : fs)
        for (size_t i = 0; i < f.pts.size(); ++i) {
            const auto a = f.pts[i], b = f.pts[(i + 1) % f.pts.size()];
            p.line(a.x, a.y + Y, b.x, b.y + Y, f.lit > .6 ? C[7] : f.lit > .3 ? C[6] : C[4]);
        }
    for (const auto& f : fs)
        if (f.spec > .96) p.sparkle(f.cx, f.cy + Y, jsRound(2 + (f.spec - .96) * 150), C[6], C[4], .5);
    const double g = std::fmod(ph * 2, 1);
    if (g < .12) p.sparkle(0, Y - 8 * 2.2 * std::cos(TIP), jsRound(5 * std::sin((g / .12) * PI)), C[6], C[4], .6);
    motes(p, t, o, Y, true);
}
}  // namespace
const Descriptor& iceCrystalDescriptor() {
    static const std::array<Option, 1> options{{{"height", 0, 256, false, 0, &PixelFxIceSettings::height}}};
    static const Descriptor            d{{PixelFxKind::IceCrystal, "ice/ice-crystal", 96, 100, 48, 92, 3, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed       = 21;
                                  s.ice.height = 34;
                                  s.palette    = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                                                  hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
                                         },
                                         draw,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
