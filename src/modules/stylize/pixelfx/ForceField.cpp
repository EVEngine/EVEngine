// eve-hot-path
// Native adaptation of pixelfx scifi/force-field.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include <unordered_map>
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, SA = .13, G = 6.3, KY = 1.12;
const double     S3 = std::sqrt(3.), CA = std::sqrt(1 - SA * SA);
struct Shot {
    double                t, lon, lat;
    bool                  big;
    std::array<double, 3> normal;
};
const std::array<Shot, 2> SHOTS = [] {
    std::array<Shot, 2> shots{{{.42, .66, .62, true, {}}, {1.5, .95, .26, false, {}}}};
    for (auto& s : shots)
        s.normal = {std::cos(s.lat) * std::sin(s.lon), std::sin(s.lat), std::cos(s.lat) * std::cos(s.lon)};
    return shots;
}();
// Use the reference runtime's ratio/quadrant reduction for spherical longitude.
// The low part completes the rounded double pi when subtracting a negative-X angle.
// Reference: Node v26.4.0 deps/v8/src/base/ieee754.cc atan2.
double longitude(double y, double x) {
    if (!std::isfinite(x) || !std::isfinite(y) || x == 0 || y == 0) return std::atan2(y, x);
    const double     a      = std::atan(std::abs(y / x));
    constexpr double piTail = 1.2246467991473531772e-16;
    return std::copysign(x > 0 ? a : PI - (a - piTail), y);
}
struct Projected {
    double x, y, f, u = 0;
};
Projected project(double R, double lon, double lat) {
    const double X = std::cos(lat) * std::sin(lon), Y = std::sin(lat), Z = std::cos(lat) * std::cos(lon);
    return {R * X, -R * (Y * CA - Z * SA), Y * SA + Z * CA};
}
struct Cell {
    int    q, r;
    double e, corner;
};
Cell cell(double gx, double gy) {
    const double fq = (S3 / 3) * gx - gy / 3, fr = (2. / 3) * gy, fs = -fq - fr;
    double       q = jsRound(fq), r = jsRound(fr);
    const double s = jsRound(fs), dq = std::abs(q - fq), dr = std::abs(r - fr), ds = std::abs(s - fs);
    if (dq > dr && dq > ds)
        q = -r - s;
    else if (dr > ds)
        r = -q - s;
    const double hx = S3 * (q + r / 2), hy = 1.5 * r, dx = gx - hx, dy = gy - hy, ax = std::abs(dx),
                 e = std::max({ax, std::abs(ax / 2 + (dy * S3) / 2), std::abs(ax / 2 - (dy * S3) / 2)}) / (S3 / 2),
                 k = jsRound((std::atan2(dy, dx) - PI / 6) / (PI / 3)), ca = PI / 6 + (k * PI) / 3;
    return {int(q), int(r), e, jsHypot2(dx - std::cos(ca), dy - std::sin(ca))};
}
struct ShellPixel {
    int    x, y, q, r, id;
    double X, Y, Z, lon, lat, e, corner, rim, sil;
    bool   seam = false, bevel = false;
};
bool   glints(uint32_t seed, int q, int r) { return rand({seed, uint32_t(q), uint32_t(r), 3}) < .2; }
double glintAt(uint32_t seed, int q, int r) {
    const double g = rand({seed, uint32_t(q), uint32_t(r)}) * 1.7;
    return g < 1.4 ? 1 + g : g - 1.4;
}
struct ForceField final : AnalyticKernel {
    std::array<std::vector<ShellPixel>, 2> shells;
    std::vector<std::vector<Projected>>    cracks;
    scifi::Ghosts                          ghosts{D, 0, std::array<double, 4>{-70, -80, 102, 14}};
    explicit ForceField(const PixelFxSettings& o) {
        const double                                R = o.scifi.radius;
        const auto&                                 C = o.palette;
        const int                                   N = int(std::ceil(R)) + 1, W = 2 * N + 1;
        std::array<std::unordered_map<int, int>, 2> ids;
        for (int y = -N; y <= std::ceil(R * SA) + 1; ++y)
            for (int x = -N; x <= N; ++x) {
                const double u = (x + .5) / R, vy = -(y + .5) / R, q = u * u + vy * vy;
                if (q > 1) continue;
                const double vz = std::sqrt(1 - q);
                for (int sign : {1, -1}) {
                    const double Y = vy * CA + sign * vz * SA, Z = -vy * SA + sign * vz * CA;
                    if (Y < 0) continue;
                    const double lon = longitude(u, Z), lat = std::asin(std::min(1., Y));
                    const auto   c    = cell(lon * G, lat * G * KY);
                    const int    id   = c.q * 1000 + c.r;
                    const size_t side = sign > 0 ? 0 : 1;
                    shells[side].push_back(
                        {x, y, c.q, c.r, id, u, Y, Z, lon, lat, c.e, c.corner, 1 - vz, (1 - std::sqrt(q)) * R});
                    ids[side][(y + N) * W + x + N] = id;
                }
            }
        for (size_t side = 0; side < 2; ++side)
            for (auto& q : shells[side]) {
                const auto different = [&](int x, int y) {
                    const auto it = ids[side].find((y + N) * W + x + N);
                    return it != ids[side].end() && it->second != q.id;
                };
                q.seam  = different(q.x + 1, q.y) || different(q.x, q.y + 1);
                q.bevel = !q.seam && (different(q.x - 1, q.y) || different(q.x, q.y - 1));
            }
        Rng         rn(o.seed * 13u + 5u);
        const auto& shot     = SHOTS[0];
        const auto  toScreen = [&](const std::vector<Point>& path, double u0, double u1) {
            std::vector<Projected> pts;
            for (size_t i = 0; i < path.size(); ++i) {
                const auto [lon, lat] = path[i];
                if (lat < .03 || lat > 1.52 || std::abs(lon) > 1.5) break;
                auto pt = project(R, lon, lat);
                pt.u    = u0 + ((u1 - u0) * double(i)) / double(path.size() - 1);
                pts.push_back(pt);
            }
            return pts;
        };
        for (int i = 0; i < 5; ++i) {
            const double a = (double(i) / 5) * TAU + rn.range(-.35, .35), L = rn.range(.42, .8);
            const auto   path = bolt(rn, shot.lon, shot.lat, shot.lon + (std::cos(a) * L) / std::cos(shot.lat),
                                     shot.lat + std::sin(a) * L, .2, 4);
            cracks.push_back(toScreen(path, 0, 1));
            const int count = rn.integer(0, 2);
            for (const auto& branch : forks(rn, path, count, L * .5, .25, .9)) {
                size_t j = 0;
                for (; j < path.size(); ++j)
                    if (path[j].x == branch.front().x && path[j].y == branch.front().y) break;
                const double u0 = j == path.size() ? 0 : double(j) / double(path.size() - 1);
                cracks.push_back(toScreen(branch, u0, u0 + .5));
            }
        }
        ghosts.sparks = o.scifi.sparks;
        Rng r(o.seed);
        for (const auto& s : SHOTS) {
            const auto [hx, hy, f, unused] = project(R, s.lon, s.lat);
            (void)f;
            (void)unused;
            const double n = s.big ? 1 : .55;
            for (int i = 0; i < 24 * n; ++i) {
                const double a = (s.big ? -.9 : -.55) + r.range(-.7, .65), sp = r.range(100, 280) * (.7 + .3 * n),
                             born = s.t + r.range(0, .02), life = r.range(.25, .75);
                ghosts.add(born, ParticleKind::Spark, hx, hy, std::cos(a) * sp, std::sin(a) * sp - 30, life);
            }
            for (int i = 0; i < 7 * n; ++i) {
                const double a = -.9 + r.range(-1.2, 1.2), sp = r.range(30, 90), born = s.t + r.range(0, .05),
                             life = r.range(.3, .6);
                const std::array<uint32_t, 3> colors{C[5], C[6], C[4]};
                const auto                    color = r.pick<uint32_t>(colors);
                ghosts.add(born, ParticleKind::Mote, hx, hy, std::cos(a) * sp, std::sin(a) * sp - 15, life, color, .7);
            }
            for (int i = 0; i < 3 * n; ++i) {
                const double x = hx + r.range(-6, 6), y = hy + r.range(-5, 5), life = r.range(.3, .5),
                             size = r.range(1, 2);
                ghosts.add(s.t + .04 + i * .07, ParticleKind::Star, x, y, 0, -4, life, C[6], size);
            }
            for (int i = 0; i < 3 * n; ++i) {
                const double x = hx + 2 + r.range(-2, 2), vx = r.range(8, 22), vy = r.range(-14, -4),
                             life = r.range(.9, 1.3), size = r.range(2.2, 3.4);
                ghosts.add(s.t + .03 + i * .04, ParticleKind::Smoke, x, hy - 1, vx, vy, life, hex(0x5b5f78), size);
            }
        }
        for (int r0 = 0; r0 < 12; ++r0)
            for (int q = -14; q <= 14; ++q) {
                if (!glints(o.seed, q, r0)) continue;
                const double lon = (S3 * (q + r0 / 2.)) / G, lat = (1.5 * r0) / (G * KY);
                const auto   pt = project(R, lon, lat);
                if (pt.f < .25 || lat < .15 || lat > 1.45 || std::abs(lon) > 1.3) continue;
                ghosts.add(glintAt(o.seed, q, r0) + .03, ParticleKind::Star, pt.x, pt.y, 0, -3, .4, C[6],
                           1.4 + rand({uint32_t(q), uint32_t(r0), 9}));
            }
    }
    void shell(Paint& p, double t, const PixelFxSettings& o, bool near) const {
        const auto&  C  = o.palette;
        const double ph = t / D, sk = clamp((ph - .72) / .26), scanLat = -.2 + sk * 2, scanK = std::sin(PI * sk);
        const std::array<double, 2> ages{scifi::since(t, SHOTS[0].t, D), scifi::since(t, SHOTS[1].t, D)};
        for (const auto& q : shells[near ? 0 : 1]) {
            const double ff = q.rim * q.rim;
            double       rp = 0, hot = 0;
            for (size_t i = 0; i < SHOTS.size(); ++i) {
                const auto&  s   = SHOTS[i];
                const double age = ages[i];
                if (age >= 1) continue;
                const double d = std::acos(clamp(q.X * s.normal[0] + q.Y * s.normal[1] + q.Z * s.normal[2], -1, 1)),
                             f = .06 + 3.1 * age, k = (1 - age) * (s.big ? 1 : .7);
                rp = std::max({rp, std::exp(-std::pow((d - f) / .15, 2)) * k, d < f ? .22 * k * k * (1 - d / f) : 0});
                const double hr = s.big ? .3 : .17;
                if (age < .55 && d < hr)
                    hot = std::max(
                        hot, age < .07 && d < hr * .75 ? 2 : std::pow(1 - age / .55, 1.5) * (1.1 - (.6 * d) / hr));
            }
            double gl = 0;
            if (glints(o.seed, q.q, q.r)) {
                const double a = scifi::since(t, glintAt(o.seed, q.q, q.r), D);
                if (a < .45) gl = 1 - a / .45;
            }
            const double sc = scanK * std::exp(-std::pow((q.lat - scanLat) / .09, 2));
            if (!near) {
                if (q.seam)
                    p.blend(q.x + .5, q.y + .5, rp > .3 ? C[5] : C[3], clamp(.18 + .25 * ff + .6 * rp + .3 * sc));
                else
                    p.blend(q.x + .5, q.y + .5, C[2], .04 + .06 * ff + .15 * rp);
                continue;
            }
            if (q.sil < 1.15) {
                const double lit = -q.X * .55 + q.Y * .85;
                p.dot(q.x, q.y, rp > .4 || lit > .8 ? WHITE : lit > .3 ? C[6] : C[5]);
                p.light(q.x, q.y, C[4], .3);
                continue;
            }
            if (hot >= 2) {
                p.dot(q.x, q.y, q.seam ? C[6] : WHITE);
                continue;
            }
            if (q.seam) {
                const double   I = .12 + .5 * ff + .08 * std::sin(q.lon * 2.3 + q.lat * 3 - ph * TAU) +
                                   (q.corner < .32 ? .18 : 0) + .9 * gl + .7 * sc + 1.3 * rp + hot;
                const uint32_t c = gl > .6 || rp > .55 || hot > .5 ? WHITE : I > .8 ? C[6] : I > .55 ? C[5] : C[4];
                if (I > .36)
                    p.dot(q.x, q.y, c);
                else
                    p.blend(q.x + .5, q.y + .5, c, .3 + I);
                p.light(q.x, q.y, C[4], .04 + .08 * I);
            } else {
                const double   a = .05 + .34 * ff * q.rim + .1 * std::pow(q.e, 4) + (q.bevel ? .12 + .2 * ff : 0) +
                                   .35 * gl + .1 * sc + .3 * rp + .6 * hot;
                const uint32_t c = hot > .6              ? WHITE
                                   : hot > .3 || gl > .5 ? C[6]
                                   : rp > .4 || q.bevel  ? C[5]
                                   : ff > .5             ? C[4]
                                                         : C[3];
                p.blend(q.x + .5, q.y + .5, c, clamp(a));
                if (rp > .1 || hot > 0 || gl > 0) p.light(q.x, q.y, C[4], .06 * (rp + hot + gl));
            }
        }
    }
    static void foot(Paint& p, double t, std::span<const uint32_t> C, double R, bool near) {
        const auto                    keep = [&](double g) { return (std::sin(g) >= 0) == near; };
        const std::array<uint32_t, 4> ramp{C[1], C[3], C[5], C[6]};
        band(p, 0, 0, R + 1, SA, 2, 1, ramp, keep);
        for (int i = 0; i < 10; ++i) {
            const double g = (double(i) / 10) * TAU + .31;
            if (!keep(g)) continue;
            const double x = std::cos(g) * (R - .5), y = std::sin(g) * R * SA,
                         k = .5 + .5 * std::cos(TAU * (2 * (t / D) - double(i) / 10));
            p.dot(x - 1, y, C[2]);
            p.dot(x + 1, y, C[2]);
            p.dot(x, y, k > .6 ? WHITE : C[5]);
            p.glow(x, y, 6 + 4 * k, C[4], (near ? .25 : .15) * (.4 + k));
        }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const double R = o.scifi.radius;
        for (double x = -R; x <= R; x += R / 3) p.glow(x, -1, 22, o.palette[3], .1);
        foot(p, t, o.palette, R, false);
        shell(p, t, o, false);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const auto&  SP = o.scifi.sparks;
        const double R  = o.scifi.radius;
        shell(p, t, o, true);
        foot(p, t, C, R, true);
        for (const auto& s : SHOTS) {
            const double age = scifi::since(t, s.t, D);
            const auto   hit = project(R, s.lon, s.lat);
            const double hx = hit.x, hy = hit.y, n = s.big ? 1 : .6;
            if (s.big && age < .9) {
                const double fade = 1 - smooth(.35, .9, age), reach = clamp(age / .09);
                for (int pass = 0; pass < 2; ++pass)
                    for (const auto& c : cracks)
                        for (size_t i = 1; i < c.size(); ++i) {
                            const auto a = c[i - 1], b = c[i];
                            if (b.u > reach || a.f < .05 || b.f < .05) continue;
                            const double k = fade * (1.2 - b.u * .4);
                            if (pass == 0) {
                                if (age < .32)
                                    for (const auto& offset : CROSS)
                                        p.line(a.x + offset.x, a.y + offset.y, b.x + offset.x, b.y + offset.y,
                                               age < .12 ? C[6] : C[5], k * (1 - age / .32));
                                continue;
                            }
                            p.line(a.x, a.y, b.x, b.y,
                                   age < .12 + b.u * .08 ? WHITE
                                   : age < .3 + b.u * .1 ? C[6]
                                                         : C[5],
                                   k);
                            if (i % 3 == 0) p.glow(b.x, b.y, 6, C[4], .35 * fade * (1 - b.u * .5));
                        }
            }
            const double tk = (t - (s.t - .1)) / .1;
            if (tk >= 0 && tk < 1) {
                const double sx = 105, sy = hy - 12, L = jsHypot2(sx - hx, sy - hy), h = L * tk;
                const auto   at = [&](double d) {
                    return Point{sx + ((hx - sx) * std::max(0., d)) / L, sy + ((hy - sy) * std::max(0., d)) / L};
                };
                const auto a = at(h), b = at(h - 2), c = at(h - 7), d = at(h - 16);
                p.line(d.x, d.y, c.x, c.y, SP[3]);
                p.line(c.x, c.y, b.x, b.y, SP[1]);
                p.line(b.x, b.y, a.x, a.y, SP[0]);
                p.dot(a.x, a.y, WHITE);
                p.glow(a.x, a.y, 9, SP[1], .55);
            }
            if (age < .14) p.sparkle(hx, hy, (7 * (1 - age / .14) + 1) * n, C[6], C[5], .4);
            const auto flash = [&](double x, double y, double r, uint32_t color, double k, double life) {
                const double a = t - s.t;
                if (a < 0 || a >= life) return;
                const double q = 1 - a / life;
                p.glow(x, y, r * (.55 + .45 * q), color, k * q * q);
            };
            flash(hx, hy, 56 * n, C[4], .45, .45);
            flash(hx, hy, 12, WHITE, .5 * n, .08);
            flash(hx + 3, hy, 22, SP[1], .4 * n, .15);
        }
        const double a0 = scifi::since(t, SHOTS[0].t, D);
        p.glow(0, -R * .45, R * 1.3, C[3], .11 + .1 * std::max(0., 1 - a0 * 2));
        ghosts.evaluate(t).draw(p);
    }
};
}  // namespace
const Descriptor& forceFieldDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"radius", .125, 256, false, 0, &PixelFxScifiSettings::radius},
                                                {"sparks", 1, 64, false, 4, &PixelFxScifiSettings::sparks}}};
    static const Descriptor            d{{PixelFxKind::ForceField, "scifi/force-field", 184, 104, 76, 84, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 5;
                                  s.actor.source = PixelFxActorSource::Dummy;
                                  s.scifi.radius = 44;
                                  s.scifi.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
                                  s.palette      = {hex(0x0b1030), hex(0x152a63), hex(0x1d4f9e), hex(0x2582d2),
                                                    hex(0x36b9ee), hex(0x8ae8ff), hex(0xd4fbff), WHITE};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<ForceField>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
