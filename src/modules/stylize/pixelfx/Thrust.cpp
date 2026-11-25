// eve-hot-path
// Native adaptation of pixelfx melee/thrust.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double OUT = .07, HOLD = .06, PULL = .2, HX = 9, HY = -20;
struct StabStreak {
    double offset, x0, t0, width;
    bool   hot;
    double length;
};
struct StabLine {
    double x, y, length, velocity, born;
};
void longStar(Paint& p, double x, double y, double length, double gain, std::span<const uint32_t> C) {
    if (length < 1) return;
    const std::array<std::array<double, 3>, 4> arms{
        {{1, 0, length}, {-1, 0, length * .7}, {0, -1, length * .42}, {0, 1, length * .42}}};
    for (const auto& arm : arms) {
        const double dx = arm[0], dy = arm[1], len = arm[2];
        for (double d = 1; d <= len; d += .5) {
            const double u     = d / len;
            const auto   color = u < .3 ? WHITE : u < .65 ? C[5] : C[4];
            p.dot(x + dx * d, y + dy * d, color, u < .7 ? 1 : 1 - (u - .7) / .35);
            if (u < .25 && len > 6) {
                p.dot(x + dx * d - dy, y + dy * d + dx, C[5]);
                p.dot(x + dx * d + dy, y + dy * d - dx, C[5]);
            }
        }
    }
    for (const auto& dir : std::array<Point, 4>{{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}})
        for (int d = 1; d <= std::min(3., length * .14); ++d) p.px(x + dir.x * d, y + dir.y * d, d < 2 ? C[5] : C[4]);
    p.px(x, y, WHITE);
    p.glow(x, y, 6 + length * 1.3, C[4], .35 * gain);
    p.glow(x, y, 3 + length * .4, WHITE, .3 * gain);
}
struct Thrust final : Kernel {
    const std::vector<uint32_t> C;
    const double                reach, TX;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::vector<StabStreak>     streaks;
    std::vector<StabLine>       lines;
    double                      t      = 0;
    bool                        struck = false;
    explicit Thrust(const PixelFxSettings& o) : C(o.palette), reach(o.melee.reach), TX(HX + reach), R(o.seed) {
        ps.sparks = o.melee.sparks;
        for (double off : {-7., -4., -2., 0., 2., 4., 7.}) {
            const double x0 = HX + 1 + R.range(0, 5) + std::abs(off) * .6, t0 = R.range(0, .02) + std::abs(off) * .003,
                         length = R.range(.75, 1);
            streaks.push_back({off, x0, t0, off == 0 ? 2. : 1., off == 0, length});
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < OUT + .08 && R.isChance(.5)) {
            const double side = R.sign(), x = HX + R.range(0, reach * .6), y = HY + side * R.range(7, 17),
                         length = R.range(18, 42), velocity = R.range(200, 340);
            lines.push_back({x, y, length, velocity, t});
        }
        for (size_t i = lines.size(); i > 0; --i)
            if (t - lines[i - 1].born > .24) lines.erase(lines.begin() + std::ptrdiff_t(i - 1));
        for (auto& line : lines) line.x += line.velocity * dt;
        if (!struck && t >= OUT) {
            struck = true;
            lights.add(TX, HY, 44, C[4], .45, .3);
            lights.add(TX, HY, 14, WHITE, .6, .06);
            for (int i = 0; i < 12; ++i) {
                const double angle = R.range(-.55, .45), speed = R.range(150, 330), life = R.range(.12, .35);
                ps.spawn(R, ParticleKind::Spark, TX, HY, std::cos(angle) * speed, std::sin(angle) * speed - 30, life);
            }
            for (int i = 0; i < 10; ++i) {
                const double base = R.range(-.9, .9), angle = base + (R.isChance(.3) ? PI : 0),
                             speed = R.range(40, 120), x = TX + R.range(-6, 2), y = HY + R.range(-2, 2),
                             life  = R.range(.25, .5);
                const auto   color = R.isChance(.5) ? C[5] : C[4];
                ps.spawn(R, ParticleKind::Mote, x, y, std::cos(angle) * speed, std::sin(angle) * speed, life, color,
                         .6);
            }
            for (int i = 0; i < 4; ++i) {
                const double x = TX + R.range(-10, 8), y = HY + R.range(-9, 9), life = R.range(.2, .4);
                const auto   color = R.isChance(.5) ? C[5] : WHITE;
                const double size = R.range(1, 2), delay = R.range(.06, .2);
                ps.spawn(R, ParticleKind::Star, x, y, 0, -3, life, color, size, delay);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        if (t < .05) p.sparkle(HX + 2, HY, 5 * (1 - t / .05) + 1, C[5], C[3], .4);
        for (const auto& line : lines) {
            const double k = (t - line.born) / .24;
            for (int d = 0; d < line.length * (1 - k * .6); ++d)
                p.dot(line.x - d, line.y,
                      d < 3                  ? WHITE
                      : d < line.length * .4 ? C[5]
                                             : C[3],
                      (1 - d / line.length) * (1 - k));
        }
        const double pull = smooth(OUT + HOLD, OUT + HOLD + PULL, t);
        for (const auto& s : streaks) {
            const double u = clamp((t - s.t0) / OUT), head = ease::outExpo(u);
            if (u <= 0 || pull >= 1) continue;
            const double y0 = HY + s.offset, end = s.x0 + (TX - s.x0) * s.length, x1 = s.x0 + (end - s.x0) * head,
                         xa  = s.x0 + (end - s.x0) * std::max(pull, head * .35 * (1 - s.length) * 3);
            const auto   yAt = [&](double x) { return y0 + (HY - y0) * clamp((x - s.x0) / (TX - s.x0)) * .85; };
            for (double x = xa; x <= x1; x += .5) {
                const double v     = (x - xa) / std::max(1., x1 - xa);
                const auto   color = v > .88   ? WHITE
                                     : s.hot   ? (v > .35 ? C[6] : C[5])
                                     : v > .55 ? C[5]
                                     : v > .25 ? C[4]
                                               : C[3];
                p.dot(x, yAt(x), color, v < .12 ? v / .12 : 1);
                if (s.width > 1 && v > .1) p.dot(x, yAt(x) + 1, v > .6 ? C[5] : C[3], v < .2 ? v / .2 : 1);
            }
        }
        if (t < OUT + HOLD + PULL)
            p.glowLine(HX + (TX - HX) * pull, HY, HX + (TX - HX) * ease::outExpo(clamp(t / OUT)), HY, 6, C[4],
                       .22 * (1 - pull));
        const double q = t - OUT;
        if (q >= 0 && q < .4) {
            const double g =
                ease::outBack(clamp(q / .04), 2.4) * (1 - smooth(.08, .4, q)) * (1 + .12 * std::sin(q * 70));
            longStar(p, TX, HY, 24 * g, 1 - q / .4, C);
        }
        if (q >= 0 && q < .2) {
            const double k = q / .2, rx = 2 + 7 * ease::outCubic(k), ry = 4 + 12 * ease::outCubic(k);
            for (double a = 0; a < TAU; a += .05)
                p.dot(TX + 4 + 10 * k + std::cos(a) * rx, HY + std::sin(a) * ry, k < .3 ? WHITE : C[5], 1.15 - k);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& thrustDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks},
                                                {"reach", 0, 512, false, 0, &PixelFxMeleeSettings::reach}}};
    static const Descriptor            d{
        {PixelFxKind::Thrust, "melee/thrust", 196, 76, 34, 62, .75, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 4;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a1608), hex(0x6b3c10), hex(0xb8701a), hex(0xf0b42e),
                              hex(0xffe27a), hex(0xfff6cf), WHITE};
            s.melee.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
            s.melee.reach  = 66;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Thrust>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
