// eve-hot-path
// Native adaptation of pixelfx status/rage.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusEyes.h"
#include "stylize/pixelfx/StatusFire.h"
#include "stylize/pixelfx/StatusVisual.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          D = 1.2, LUB = .05, DUB = .29, G = 760;
constexpr std::array<std::string_view, 7> SMALL{"..#.#..", "..#.#..", "##...##", ".......",
                                                "##...##", "..#.#..", "..#.#.."};
constexpr std::array<std::string_view, 9> BIG{"...#.#...", "...#.#...", "..##.##..", "###...###", ".........",
                                              "###...###", "..##.##..", "...#.#...", "...#.#..."};
double                                    pulse(double t, double at, double width = .06) {
    const double q = (std::fmod(std::fmod(t - at, D) + D + D / 2, D) - D / 2) / width;
    return std::exp(-q * q);
}
double beat(double t) { return pulse(t, LUB) + .65 * pulse(t, DUB); }
bool   isShaking(double t) { return wrap(t - (LUB - .02), D) < .13 || wrap(t - (DUB - .02), D) < .09; }
struct Rage final : AnalyticKernel {
    const status::Shape             shape;
    const std::vector<status::Eye>  eyeList;
    std::vector<std::vector<Point>> cracks;
    explicit Rage(const PixelFxSettings& o) : shape(o.actor), eyeList(status::eyes(o.actor, shape)) {
        const auto&                                a   = o.actor;
        const auto                                 row = shape.rows[size_t(shape.height - 1)];
        Rng                                        r(o.seed * 31u + 7u);
        const std::array<std::array<double, 2>, 4> roots{{{a.x + row[0] + 2, -1},
                                                          {a.x + row[1] - 1, 1},
                                                          {a.x + double(row[0] + row[1]) / 2, -1},
                                                          {a.x + double(row[0] + row[1]) / 2 + 1, 1}}};
        for (const auto& root : roots) {
            double             x = root[0], y = .5;
            const double       dir = root[1];
            std::vector<Point> points{{x, y}};
            const int          count = r.integer(5, 9);
            for (int i = 0; i < count; ++i) {
                x += dir * r.range(1.2, 2.4);
                y = clamp(y + r.range(-.9, .9), 0, 2.5);
                points.push_back({x, y});
                if (i > 1 && r.isChance(.3)) {
                    const double bx = x + dir * r.range(1, 2), by = clamp(y + r.signedRange(.6, 1.4), 0, 3);
                    cracks.push_back({{x, y}, {bx, by}});
                }
            }
            cracks.push_back(std::move(points));
        }
    }
    PixelFxActorVisibility actorVisibility(double t) const override {
        return isShaking(t) ? PixelFxActorVisibility::Hidden : PixelFxActorVisibility::Visible;
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const auto&  H = o.status.heat;
        const auto&  a = o.actor;
        const double b = beat(t), stamp = std::exp(-wrap(t - LUB, D) / .18);
        cycle(t, D, 9, o.seed + 2u, .7, 1.1, [&](double age, double life, Rng& r, int) {
            const double u = age / life, x0 = r.range(-16, 16), y0 = r.range(10, 40), speed = r.range(14, 22),
                         y = a.y + y0 - age * speed, length = r.range(6, 11), phase = r.range(0, TAU),
                         k = std::sin(PI * u) * .55;
            for (int d = 0; d < length; ++d)
                p.dot(x0 - length / 2 + d, y + jsRound(std::sin(d * .9 + phase + age * 12) * .8), d % 3 ? C[2] : C[1],
                      k * (1 - std::abs(d / length - .5) * 1.4));
        });
        p.shader(-30, -4, 30, 8, [&](double x, double y, ShadeOutput& out) {
            const double d = jsHypot2(x / 28, (y - .5) / 4.5);
            if (d < 1) {
                out.l = C[3];
                out.k = (.2 + .3 * b) * (1 - d) * (1 - d);
            }
        });
        const double heat = .25 + .75 * stamp;
        for (const auto& points : cracks)
            for (size_t i = 1; i < points.size(); ++i) {
                const auto   a0 = points[i - 1], a1 = points[i];
                const double far = double(i) / double(points.size());
                p.line(a0.x, a0.y, a1.x, a1.y,
                       heat * (1 - far * .6) > .6    ? H[3]
                       : heat * (1 - far * .5) > .35 ? H[2]
                                                     : H[1]);
                if (i % 2) p.glow(a1.x, a1.y, 4, H[2], .25 * heat * (1 - far * .5));
            }
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C       = o.palette;
        const auto&  H       = o.status.heat;
        const auto&  a       = o.actor;
        const double b       = beat(t);
        const bool   shaking = isShaking(t);
        const int    dx      = shaking ? (int(std::floor(t * 60)) % 2 ? 1 : -1) : 0,
                     dy      = shaking && wrap(t - (LUB - .02), D) < .06 ? -1 : 0;
        for (const auto& q : shape.body) {
            const uint32_t c     = status::colorAt(a, q[0], q[1]);
            const double   level = luma(c);
            const bool     skin  = level > .55 && (c & 255u) > ((c >> 16) & 255u) + 30u;
            if (skin)
                p.px(a.x + q[0] + dx, a.y + q[1] + dy, mix(c, C[4], .22 + .3 * b));
            else if (shaking)
                p.px(a.x + q[0] + dx, a.y + q[1] + dy, c);
        }
        for (const auto& e : eyeList) {
            const double ex = a.x + dx, ey = a.y + dy;
            const int    inner = double(e.x0 + e.x1) / 2 < double(a.width) / 2 ? 1 : -1;
            for (const auto& q : e.pixels)
                p.px(ex + q[0], ey + q[1], q[1] == e.y0 ? C[7] : q[1] == e.y0 + 1 ? C[6] : C[4]);
            const int xi = inner > 0 ? e.x1 : e.x0, xo = inner > 0 ? e.x0 : e.x1;
            p.px(ex + xo - inner, ey + e.y0 - 2, 0xff1f1726u);
            p.px(ex + xo, ey + e.y0 - 2, 0xff1f1726u);
            p.px(ex + xi, ey + e.y0 - 1, 0xff1f1726u);
            p.glow(ex + double(e.x0 + e.x1 + 1) / 2, ey + e.y0 + 1, 5, C[5], .35 + .35 * b);
        }
        const bool   big = pulse(t, LUB + .02, .05) > .35 || pulse(t, DUB + .02, .04) > .6;
        const double mx = a.x + a.width * .86 + dx, my = a.y + 3 + dy;
        status::icon(p, mx, my, 5,
                     [&](int x, int y) { return big ? status::bitmap(BIG, x, y) : status::bitmap(SMALL, x, y); },
                     {C[0], C[3], C[4], C[5]});
        p.px(mx - (big ? 3 : 2), my - (big ? 3 : 2), C[6]);
        p.glow(mx, my, 8, C[4], .25 + .4 * b);
        for (const auto& jet : std::array<std::array<double, 3>, 2>{{{LUB + .04, 7, 1}, {DUB + .04, 4, .75}}})
            for (int side : {-1, 1})
                for (int i = 0; i < jet[1]; ++i) {
                    const double age  = wrap(t - (jet[0] + i * .016), D),
                                 life = .22 + .12 * rand({uint32_t(i), uint32_t(side), 3u});
                    if (age >= life) continue;
                    const double u     = age / life,
                                 angle = -PI / 2 + side * (.5 + .2 * rand({uint32_t(i), uint32_t(side), 5u})),
                                 speed = (90 + 40 * rand({uint32_t(i), uint32_t(side), 7u})) * jet[2],
                                 x     = side * a.width * .36 + std::cos(angle) * status::travel(speed, 0, 7, age),
                                 y     = a.y + 3 + std::sin(angle) * status::travel(speed, 0, 7, age) - 8 * age,
                                 radius =
                                     (1 + 3.2 * std::sqrt(u)) * (.8 + .4 * rand({uint32_t(i), uint32_t(side), 9u})),
                                 k = std::min(1., u * 12) * std::pow(1 - u, 2);
                    p.disc(x, y, radius, o.status.steam, .7 * k);
                    p.disc(x - radius * .2, y - radius * .25, radius * .6, WHITE, .6 * k);
                    if (u < .3) p.dot(x, y, WHITE, 1 - u / .3);
                }
        for (const auto& flash : std::array<std::array<double, 3>, 2>{{{LUB, 8, 7}, {DUB, 6, 5}}}) {
            const double age = wrap(t - flash[0], D);
            if (age > .2) continue;
            const double k = age / .2;
            for (int i = 0; i < flash[1]; ++i) {
                const double angle = -PI / 2 + ((i + .5) / flash[1] - .5) * 4.4, r0 = 15 + 6 * k, ca = std::cos(angle),
                             sa = std::sin(angle), cy = a.y + 12;
                std::array<Point, 4> points;
                for (int j = 0; j <= 3; ++j) {
                    const double radius = r0 + (double(j) / 3) * flash[2], off = j % 2 ? 1.3 : -.4;
                    points[size_t(j)] = {ca * radius - sa * off, cy + sa * radius + ca * off};
                }
                p.polyline(points, [&](double u) { return u > .66 ? C[7] : u > .33 ? C[5] : C[4]; }, 1.2 - k);
                p.glowLine(points[0].x, points[0].y, points[3].x, points[3].y, 3, C[4], .3 * (1 - k));
            }
        }
        const double st = wrap(t - LUB, D);
        for (int i = 0; i < 10; ++i) {
            const double life = .5 + .5 * rand({uint32_t(i), 11u}), age = st - .02 * rand({uint32_t(i), 12u});
            if (age < 0 || age > life) continue;
            const double x = (rand({uint32_t(i), 13u}) - .5) * 30 + std::sin(age * 9 + i) * 1.5 * age,
                         y = -1 + status::travel(-(40 + 50 * rand({uint32_t(i), 14u})), -20, 1.6, age);
            status::ember(p, x, y, age / life, {H[3], H[2], H[1]}, age + i);
        }
        for (int i = 0; i < 12; ++i) {
            const double vx = (rand({uint32_t(i), 21u}) - .5) * 90, vy = -(40 + 70 * rand({uint32_t(i), 22u})),
                         x = (rand({uint32_t(i), 23u}) - .5) * 18 + vx * st, y = -1 + vy * st + .5 * G * st * st;
            if (st > .6 || y > 0) continue;
            p.px(x, y, i % 3 ? C[2] : 0xff6a5e72u);
        }
        p.glow(0, a.y + 10, 26, C[4], .12 + .4 * b);
        p.glow(0, a.y + 22, 34, C[3], .08 + .18 * b);
    }
};
}  // namespace
const Descriptor& rageDescriptor() {
    static const std::array<Option, 3> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"heat", 1, 64, false, 4, &PixelFxStatusSettings::heat},
         {"steam", 0, 4294967295., false, 0, &PixelFxStatusSettings::steam, true}}};
    static const Descriptor d{
        {PixelFxKind::Rage, "status/rage", 80, 80, 40, 70, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 6;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x1a0208), hex(0x4a0612), hex(0x8a0c1c), hex(0xc81828),
                              hex(0xff3040), hex(0xff7a7a), hex(0xffc8c8), WHITE};
            s.status.heat  = {hex(0x5a0a0a), hex(0xc8281a), hex(0xff7a2a), hex(0xffd27a)};
            s.status.steam = hex(0xf2e8ee);
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Rage>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
