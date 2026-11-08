// eve-hot-path
// Native adaptation of pixelfx status/confused.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusEyes.h"
#include "stylize/pixelfx/StatusVisual.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                           D = 2.4;
constexpr std::array<std::string_view, 7>  Q0{".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."};
constexpr std::array<std::string_view, 9>  Q1{".####.", "##..##", "....##", "...##.", "..##..",
                                              "..##..", "......", "..##..", "..##.."};
constexpr std::array<std::string_view, 10> Q2{".#####.", "##...##", "##...##", ".....##", "....##.",
                                              "...##..", "...##..", ".......", "...##..", "...##.."};
char                                       question(int size, int dx, int dy) {
    if (size == 0) return status::bitmap(Q0, dx, dy);
    if (size == 1) return status::bitmap(Q1, dx, dy);
    return status::bitmap(Q2, dx, dy);
}
struct Mark {
    double x, y, time;
    int    big;
};
constexpr std::array<Mark, 3> marks{{{-12, -5, 0, 2}, {12, -8, .8, 1}, {1, -17, 1.6, 2}}};
void                          spiral(Paint& p, std::span<const uint32_t> C, double cx, double cy, double t) {
    const double rot = (t / D) * TAU * 2, breathe = 1 + .08 * std::sin((t / D) * TAU * 4);
    const auto   at = [&](double u) {
        const double th = u * 1.8 * TAU - rot, r = (.8 + 6.6 * u) * breathe;
        return std::array<double, 2>{cx + std::cos(th) * r, cy + std::sin(th) * r * .9};
    };
    for (int i = 1; i <= 72; ++i) {
        const double u = double(i) / 72;
        const auto   a = at(double(i - 1) / 72), b = at(u);
        if (u > .45) p.line(a[0] + .5, a[1] + .7, b[0] + .5, b[1] + .7, C[1]);
    }
    for (int i = 1; i <= 72; ++i) {
        const double u = double(i) / 72;
        const auto   a = at(double(i - 1) / 72), b = at(u);
        p.line(a[0], a[1], b[0], b[1], u > .88 ? C[5] : u > .62 ? C[4] : u > .3 ? C[3] : C[2]);
        if (i % 6 == 0) p.light(b[0], b[1], C[3], .4 * u);
    }
    const auto tip = at(1);
    p.sparkle(tip[0], tip[1], 2, C[4], C[3], .5);
    p.glow(cx, cy, 13, C[3], .28);
}
struct Confused final : AnalyticKernel {
    const std::vector<status::Eye> eyeList;
    explicit Confused(const PixelFxSettings& o) : eyeList(status::eyes(o.actor, status::Shape(o.actor))) {}
    void drawBack(Paint& p, double, const PixelFxSettings& o) const override {
        p.glow(0, o.actor.y - 7, 16, o.palette[3], .22);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C    = o.palette;
        const auto& M    = o.status.marks;
        const auto& a    = o.actor;
        const int   look = int(std::floor((t / D) * 6)) % 2 ? 1 : -1;
        for (const auto& e : eyeList) {
            for (const auto& q : e.pixels) p.px(a.x + q[0], a.y + q[1], 0xff17121fu);
            p.px(a.x + (look < 0 ? e.x0 : e.x1), a.y + e.y0, WHITE);
        }
        spiral(p, C, 0, a.y - 7, t);
        for (const auto& m : marks) {
            const double age = wrap(t - m.time, D), life = 1.9;
            if (age > life) continue;
            const double u = age / life, pop = ease::outBack(clamp(age / .16), 3), out = clamp((u - .82) / .18);
            const int    size = int(std::max(0., jsRound(m.big * pop * (1 - out) + (pop > 1.1 ? 1 : 0))));
            const double bob = std::sin(age * 5 + m.time * 3) * 1.6, tilt = std::sin(age * 3.3 + m.time) * .8;
            if (pop < .3 || (out > 0 && size == 0 && out > .6)) continue;
            const double x = m.x + tilt, y = a.y + m.y + bob - age * 2;
            if (age < .2) {
                const double k = age / .2;
                for (int j = 0; j < 8; ++j) {
                    const double angle = (double(j) / 8) * TAU + .2, d0 = 4 + 7 * k, d1 = d0 + 3 * (1 - k);
                    p.line(x + std::cos(angle) * d0, y + std::sin(angle) * d0, x + std::cos(angle) * d1,
                           y + std::sin(angle) * d1, j % 2 ? M[2] : M[3], 1 - k);
                }
                p.glow(x, y, 16, M[2], .45 * (1 - k));
            }
            status::icon(p, x, y, 7, [&](int dx, int dy) { return question(std::min(2, size), dx, dy); },
                         {M[0], M[1], M[2], M[3]});
            p.glow(x, y, 10 + size * 2, M[2], .28 * (1 - out));
        }
        cycle(t, D, 10, o.seed + 2u, .35, .6, [&](double age, double life, Rng& r, int) {
            const double th = r.range(0, TAU), u = age / life, d = 6 + age * r.range(12, 22), x = std::cos(th) * d,
                         y = a.y - 7 + std::sin(th) * d * .8;
            if (std::sin(PI * u) > .6 && r.next() < .5)
                p.sparkle(x, y, 1, C[4], C[3], .3);
            else
                p.dot(x, y, u < .5 ? C[5] : C[4], 1.2 - u);
        });
    }
};
}  // namespace
const Descriptor& confusedDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"marks", 1, 64, false, 4, &PixelFxStatusSettings::marks}}};
    static const Descriptor            d{
        {PixelFxKind::Confused, "status/confused", 60, 78, 30, 70, D, true},
        6,
        [](PixelFxSettings& s) {
            s.seed         = 6;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x1e0f2e), hex(0x4b2070), hex(0x8a3ab0), hex(0xc66ae0), hex(0xf0a8ff), WHITE};
            s.status.marks = {hex(0x2a1608), hex(0xb0701a), hex(0xffd84a), hex(0xfff4b8)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Confused>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
