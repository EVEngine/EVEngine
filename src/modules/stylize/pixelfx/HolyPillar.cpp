// eve-hot-path
// Native adaptation of pixelfx holy/holy-pillar.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/HolyFeathers.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double TOP = -204, PY = -1, SUN = TOP + 38, SY = .24, RX = 26, OPEN = .3, HIT = .66, FOLD = 2.05, SHUT = 2.45,
                 LIFT = 2.75, DMAX = PY - TOP;
struct Shaft {
    double theta, spread, brightness, phase, far;
};
struct Mote {
    double x, y, velocity, amplitude, frequency, phase;
};
struct HolyPillar final : Kernel {
    const std::vector<uint32_t>   C;
    const std::array<uint32_t, 6> wing;
    std::vector<Shaft>            shafts;
    std::array<Mote, 72>          motes;
    Rng                           rng;
    Particles                     ps;
    Lights                        lights;
    holy::Feathers                feathers;
    double                        t   = 0;
    bool                          hit = false, shut = false, glinted = false;
    explicit HolyPillar(const PixelFxSettings& o)
        : C(o.palette), wing{C[2], C[3], C[4], C[5], C[6], WHITE}, rng(o.seed) {
        ps.sparks        = {WHITE, C[6], C[5], C[3]};
        const int    N   = o.holy.shafts;
        const double mid = double(N - 1) / 2;
        shafts.reserve(size_t(N));
        for (int i = 0; i < N; ++i) {
            const double off = i - mid, far = std::abs(off) / mid, theta = off * (.4 / mid) + rng.range(-.02, .02),
                         spread     = rng.range(.05, .085) * (1 - far * .25),
                         brightness = (1 - far * .35) * rng.range(.8, 1), phase = rng.range(0, 1);
            shafts.push_back({theta, spread, brightness, phase, far});
        }
        for (auto& m : motes)
            m = {rng.range(-80, 80), rng.range(0, 1),    rng.range(7, 18),
                 rng.range(.5, 2.5), rng.range(.6, 1.5), rng.range(0, TAU)};
        for (int i = 0; i < 6; ++i) {
            const double x = rng.range(-46, 46), y = rng.range(-150, -100), vx = rng.range(-8, 8), vy = rng.range(0, 8),
                         a = rng.range(-.4, .4), len = rng.range(7, 9), wid = rng.range(1.9, 2.3),
                         life = rng.range(1.4, 1.7), sink = rng.range(26, 36),
                         delay = HIT + .1 + i * .16 + rng.range(0, .08);
            feathers.add(rng, {x, y, vx, vy, a, len, wid, life, delay, i % 3 == 0, sink});
        }
    }
    double angle(const Shaft& s) const {
        const double st = s.far * .1, open = ease::outBack(seg(t, OPEN + st, OPEN + st + .42), 1.5),
                     fold = ease::inOutCubic(seg(t, FOLD - s.far * .1, SHUT));
        return s.theta * open * (1 - fold);
    }
    double frontD(const Shaft& s) const {
        return DMAX * (1 - ease::inQuad(seg(t, OPEN + s.far * .06, HIT + s.far * .06)));
    }
    double liftD() const { return DMAX * ease::inQuad(seg(t, SHUT + .05, LIFT)); }
    double fade() const { return smooth(OPEN, OPEN + .1, t) * (1 - smooth(LIFT - .05, LIFT + .05, t)); }
    double width(const Shaft& s, double d) const { return (1.4 + d * s.spread) * lerp(1, .25, smooth(SHUT, LIFT, t)); }
    double pulse(const Shaft& s, double d) const {
        if (t < HIT || t > SHUT) return 1;
        const double q = (t - HIT) / .6 + s.phase, D = DMAX * (1 - (q - std::floor(q)));
        return 1 + .9 * std::exp(-std::pow((d - D) / 12, 2));
    }
    double lightAt(double x, double y) const {
        const double dy = PY - y;
        if (dy <= 0) return 0;
        double I = 0;
        for (const auto& s : shafts) {
            const double a = angle(s), d = dy / std::cos(a), xc = std::tan(a) * dy, hw = width(s, d) / 2 / std::cos(a);
            if (d < liftD() || d < frontD(s)) continue;
            const double v = (x - xc) / hw;
            if (std::abs(v) < 1) I += (1 - v * v) * s.brightness;
        }
        return I * fade();
    }
    void fan(Paint& p) const {
        const double k = fade();
        if (k <= 0) return;
        const double lift = liftD();
        // Source Float32/Uint8 assignment happens after every shaft contribution.
        std::array<float, 192>   row{};
        std::array<uint8_t, 192> core{};
        for (int y = int(std::ceil(TOP)); y < PY; ++y) {
            const double dy = PY - (y + .5);
            row.fill(0);
            core.fill(0);
            bool any = false;
            for (const auto& s : shafts) {
                const double a = angle(s), ca = std::cos(a), d = dy / ca;
                if (d < lift || d < frontD(s) - 6) continue;
                const double xc = std::tan(a) * dy, hw = width(s, d) / 2 / ca,
                             soft = smooth(frontD(s) - 6, frontD(s) + 4, d) * smooth(lift, lift + 6, d),
                             I0   = s.brightness * pulse(s, d) * soft * k;
                for (int x = int(std::floor(xc - hw)); x <= std::ceil(xc + hw); ++x) {
                    const double v = (x + .5 - xc) / hw;
                    const int    i = x + 96;
                    if (i < 0 || i >= 192 || std::abs(v) >= 1) continue;
                    row[size_t(i)] = float(double(row[size_t(i)]) + I0 * std::pow(1 - v * v, 1.3));
                    any            = true;
                }
                const int ci = int(jsRound(xc - .5)) + 96;
                if (ci >= 0 && ci < 192)
                    core[size_t(ci)] = uint8_t(std::max(double(core[size_t(ci)]), std::min(255., I0 * 200)));
            }
            if (!any) continue;
            for (size_t i = 0; i < 192; ++i) {
                const double I = row[i];
                if (I < .02) continue;
                const int x = int(i) - 96;
                p.blend(x, y, C[6], std::min(.6, I * .38));
                p.light(x, y, C[4], std::min(.9, I * .32));
                if (core[i]) p.dot(x, y, core[i] > 150 ? WHITE : C[6], std::min(1., double(core[i]) / 180));
            }
        }
        if (t > HIT - .05) p.glow(0, PY - 3, 20, C[5], .5 * k);
    }
    void sun(Paint& p) const {
        const double r = 15 * ease::outBack(seg(t, .02, .45), 1.8) * (1 - ease::inBack(seg(t, SHUT, LIFT + .1), 1.5));
        if (r > .5) {
            const double spin = t * .6;
            p.glow(0, SUN, std::min(32., r * 3), C[4], .5);
            p.glow(0, SUN, r * 1.4, WHITE, .35);
            p.circle(0, SUN, r * .45, WHITE);
            const auto keep = [](double) { return true; };
            holy::oval(p, 0, SUN, r * .62, r * .62, C[6], 1., keep);
            holy::oval(
                p, 0, SUN, r, r, [&](double g) { return std::sin(g * 6 + spin * 4) > .5 ? WHITE : C[5]; }, 1., keep);
            for (int j = 0; j < 16; ++j) {
                const double g = spin + (j * TAU) / 16, l = j % 2 ? 3 : 6;
                p.lineFn(
                    std::cos(g) * (r + 2), SUN + std::sin(g) * (r + 2), std::cos(g) * (r + 2 + l),
                    SUN + std::sin(g) * (r + 2 + l), [&](double u) { return u < .4 ? C[6] : C[4]; },
                    [](double u) { return 1.1 - u; });
            }
        }
        if (t > LIFT && t < LIFT + .3) p.sparkle(0, SUN, 10 * (1 - (t - LIFT) / .3), WHITE, C[5], .6);
    }
    void sigil(Paint& p, bool back) const {
        if (t < HIT) return;
        const double u = seg(t, HIT, HIT + .2), heat = 1 - smooth(SHUT, SHUT + .8, t),
                     out = 1 - smooth(SHUT + .4, SHUT + 1, t);
        if (out <= 0) return;
        const auto keep  = [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; };
        const auto burnt = [&](double g) {
            return std::abs(std::fmod(std::fmod(g - PI / 2 + PI, TAU) + TAU, TAU) - PI) <= u * PI + .01;
        };
        const auto col = [&](double g, bool hot) -> uint32_t {
            if (!burnt(g)) return 0;
            const bool flick = rand({jsUint(jsRound(g * 40)), jsUint(std::floor(t * 20))}) < .18;
            return t < HIT + .08 || (flick && heat > .4) ? WHITE
                   : heat > .6                           ? (hot ? C[6] : C[5])
                   : heat > .3                           ? C[4]
                   : heat > .1                           ? C[3]
                                                         : C[2];
        };
        const auto cut = [&](double g) { return keep(g) && burnt(g); };
        holy::oval(p, 0, 0, RX, RX * SY, [&](double g) { return col(g, true); }, out, cut);
        holy::oval(p, 0, 0, RX * .72, RX * .72 * SY, [&](double g) { return col(g, false); }, out, cut);
        for (int j = 0; j < 12; ++j) {
            const double g = t * .35 + (j * TAU) / 12;
            if (!keep(g) || !burnt(g)) continue;
            const auto   c  = col(g, j % 2 == 0);
            const double r0 = RX * .78, r1 = RX * (j % 3 == 0 ? .95 : .88);
            if (c) p.line(std::cos(g) * r0, std::sin(g) * r0 * SY, std::cos(g) * r1, std::sin(g) * r1 * SY, c, out);
        }
        if (!back) {
            for (int i = -2; i <= 2; ++i)
                p.glow(i * RX * .45, -2, 16, C[4], (.34 - std::abs(i) * .05) * out * (.5 + .5 * heat));
            p.glow(0, -1, 10, WHITE, .3 * heat * out);
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < OPEN + .1 && rng.isChance(.5)) {
            const double a = rng.range(0, TAU), D = rng.range(22, 34), x = std::cos(a) * D, y = SUN + std::sin(a) * D,
                         T = rng.range(.18, .28), v = ((D - 10) * 3) / (std::exp(3 * T) - 1);
            const auto   c = rng.isChance(.5) ? C[6] : C[5];
            ps.spawn(rng, ParticleKind::Streak, x, y, (-x / D) * v, ((SUN - y) / D) * v, T, c);
        }
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(0, -30, 64, C[5], .85, .5);
            lights.add(0, -6, 30, WHITE, .8, .12);
            for (int i = 0; i < 26; ++i) {
                const double a = -PI / 2 + rng.range(-1.2, 1.2), sp = rng.range(70, 190), x = rng.range(-3, 3),
                             life = rng.range(.25, .6);
                ps.spawn(rng, ParticleKind::Spark, x, -2, std::cos(a) * sp, std::sin(a) * sp, life);
            }
        }
        if (t > HIT && t < SHUT + .1 && rng.isChance(.35)) {
            const double g = rng.range(0, TAU), r = RX * (rng.isChance(.5) ? 1 : .72), vx = rng.range(-6, 6),
                         vy = -rng.range(12, 30), life = rng.range(.5, .8);
            ps.spawn(rng, ParticleKind::Ember, std::cos(g) * r, std::sin(g) * r * SY - 1, vx, vy, life, C[5], 1, 0,
                     std::sin(g) < 0 ? ParticleFlag::Back : ParticleFlag::None);
        }
        if (t > HIT && t < FOLD && rng.isChance(.05)) {
            const double x = rng.range(-1, 1) * 40, y = -rng.range(20, 160), vy = rng.range(2, 8),
                         life = rng.range(.3, .6);
            const auto   c    = rng.isChance(.5) ? WHITE : C[6];
            const double size = rng.range(.8, 1.8);
            ps.spawn(rng, ParticleKind::Star, x, y, 0, vy, life, c, size);
        }
        if (!shut && t >= SHUT) {
            shut = true;
            lights.add(0, -60, 70, C[5], .7, .35);
            for (int i = 0; i < 12; ++i) {
                const double x = rng.range(-4, 4), y = -rng.range(10, 170), vx = rng.range(-25, 25),
                             vy = rng.range(-10, 10), life = rng.range(.4, .8);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.8, 2);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, c, size);
            }
        }
        if (!glinted && t >= LIFT) {
            glinted = true;
            lights.add(0, SUN, 40, WHITE, .6, .2);
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        feathers.update(dt);
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        fan(p);
        sigil(p, true);
        feathers.draw(p, wing, Paint::Layer::Back);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        sigil(p, false);
        for (const auto& m : motes) {
            const double y = TOP + std::fmod(std::fmod(m.y * DMAX + t * m.velocity, DMAX) + DMAX, DMAX),
                         x = m.x + std::sin(t * m.frequency + m.phase) * m.amplitude, I = lightAt(x, y);
            if (I < .2) continue;
            p.dot(x, y, I > .7 ? WHITE : C[6], std::min(1., I * 1.3));
            p.light(x, y, C[5], .4 * std::min(1., I));
        }
        sun(p);
        if (t > HIT - .02 && t < HIT + .25) p.sparkle(0, PY - 2, 12 * (1 - (t - HIT) / .25), WHITE, C[5], .7);
        if (t > SHUT && t < SHUT + .2) p.sparkle(0, PY - 2, 8 * (1 - (t - SHUT) / .2), WHITE, C[5], .5);
        feathers.draw(p, wing, Paint::Layer::Front);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& holyPillarDescriptor() {
    static const std::array<Option, 1> options{{{"shafts", 3, 64, false, 0, &PixelFxHolySettings::shafts}}};
    static const Descriptor            d{
        {PixelFxKind::HolyPillar, "holy/holy-pillar", 192, 232, 96, 204, 3.5, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 3;
            s.holy.shafts = 7;
            s.palette     = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                             hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HolyPillar>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
