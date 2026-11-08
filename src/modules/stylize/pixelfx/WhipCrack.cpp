// eve-hot-path
// Native adaptation of pixelfx melee/whip-crack.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double ROLL0 = .02, ROLL = .19, CRACK = ROLL0 + ROLL, RECOIL = .16, HX = 11, HY = -21;
struct WhipSliver {
    double angle, radius, speed, arc;
};
struct TipSample {
    Point  point;
    double time;
};
struct WhipCrack final : Kernel {
    const std::vector<uint32_t> C;
    const double                L;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::array<melee::Spike, 8> spikes;
    std::array<WhipSliver, 4>   slivers;
    std::vector<TipSample>      tipTrail;
    double                      t       = 0;
    bool                        cracked = false;
    explicit WhipCrack(const PixelFxSettings& o) : C(o.palette), L(o.melee.length), R(o.seed) {
        ps.sparks = o.melee.sparks;
        const std::array<double, 8> angles{0, .8, 1.6, 2.4, 3.14, 3.9, 4.7, 5.5};
        for (size_t i = 0; i < spikes.size(); ++i) {
            const double a   = angles[i] + R.range(-.15, .15),
                         len = (i == 0  ? 13.
                                : i % 2 ? 6.
                                        : 9.) +
                               R.range(0, 3),
                         w   = 1.4 + R.range(0, .5);
            spikes[i]        = {a, len, w};
        }
        size_t i = 0;
        for (double a : {-1.2, -.3, .5, 1.3}) {
            const double angle = a + R.range(-.2, .2), radius = R.range(4, 6), speed = R.range(80, 120),
                         arc = R.range(.4, .7);
            slivers[i++]     = {angle, radius, speed, arc};
        }
        tipTrail.reserve(9);
    }
    double lam(double at) const { return L * (.34 + .66 * ease::inQuad(clamp((at - ROLL0) / ROLL))); }
    Point  shapePoint(double s, double l, double r, double recoil) const {
        double x, y;
        if (s <= l) {
            x = HX + s;
            y = HY - std::sin((PI * s) / std::max(l, 1.)) * 2.5 * (1 - l / L);
        } else if (s <= l + PI * r) {
            const double th = (s - l) / r;
            x               = HX + l + std::sin(th) * r;
            y               = HY + r - std::cos(th) * r;
        } else {
            x = HX + l - (s - l - PI * r);
            y = HY + 2 * r;
        }
        if (recoil > 0) y += std::sin((PI * s) / L) * 6 * recoil;
        return {x, y};
    }
    template <class Fn>
    void shape(double at, Fn fn) const {
        const double l = lam(at), r = 6.5 - 3.5 * (l / L), recoil = smooth(CRACK + .04, CRACK + .04 + RECOIL, at),
                     len = L * (1 - recoil * .85);
        for (double s = 0; s <= len; s += 1) fn(size_t(s), shapePoint(s, l, r, recoil));
    }
    Point lastPoint(double at) const {
        const double l = lam(at), r = 6.5 - 3.5 * (l / L), recoil = smooth(CRACK + .04, CRACK + .04 + RECOIL, at),
                     len = L * (1 - recoil * .85);
        return shapePoint(std::floor(len), l, r, recoil);
    }
    void update(double dt) override {
        t += dt;
        if (t > CRACK - .05 && t < CRACK + .01) tipTrail.push_back({lastPoint(t), t});
        for (size_t i = tipTrail.size(); i > 0; --i)
            if (t - tipTrail[i - 1].time > .06) tipTrail.erase(tipTrail.begin() + std::ptrdiff_t(i - 1));
        if (!cracked && t >= CRACK) {
            cracked = true;
            lights.add(HX + L, HY - 1, 46, C[4], .6, .3);
            lights.add(HX + L, HY - 1, 14, WHITE, .8, .07);
            for (int i = 0; i < 16; ++i) {
                const double a = R.range(-1.3, 1.2), speed = R.range(130, 300), life = R.range(.12, .36);
                ps.spawn(R, ParticleKind::Spark, HX + L, HY - 1, std::cos(a) * speed, std::sin(a) * speed - 40, life);
            }
            for (int i = 0; i < 3; ++i) {
                const double x = HX + L + R.range(-2, 3), y = HY - 1 + R.range(-2, 2), vx = R.range(10, 40),
                             vy = -R.range(6, 18), life = R.range(.5, .8), size = R.range(2.5, 4),
                             delay = R.range(.02, .06);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, C[2], size, delay);
            }
            for (int i = 0; i < 4; ++i) {
                const double x = HX + L + R.range(-8, 10), y = HY - 1 + R.range(-8, 8), life = R.range(.2, .4);
                const auto   color = R.isChance(.5) ? C[5] : WHITE;
                const double size = R.range(.8, 1.8), delay = R.range(.05, .2);
                ps.spawn(R, ParticleKind::Star, x, y, 0, -3, life, color, size, delay);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double end = CRACK + .04 + RECOIL;
        if (t < end) {
            const double fade = 1 - smooth(end - .06, end, t);
            if (t < CRACK)
                for (const auto& v : std::array<std::array<double, 3>, 2>{{{.03, 1, .5}, {.015, 2, .7}}})
                    shape(std::max(0., t - v[0]), [&](size_t i, Point a) {
                        if (i % 2 == 1) p.dot(a.x, a.y, C[size_t(v[1])], v[2]);
                    });
            const double l = lam(t), r = 6.5 - 3.5 * (l / L), recoil = smooth(CRACK + .04, CRACK + .04 + RECOIL, t),
                         len = L * (1 - recoil * .85), n = std::floor(len) + 1;
            Point        previous{};
            shape(t, [&](size_t i, Point a) {
                if (i > 0) {
                    const double u = double(i) / n, s = double(i);
                    const bool   inLoop = t < CRACK && s > l && s < l + PI * r,
                                 lead   = inLoop && std::abs(s - l - (PI * r) / 2) < r * .9;
                    p.line(previous.x, previous.y, a.x, a.y, lead ? WHITE : inLoop ? C[5] : u < .5 ? C[4] : C[3], fade);
                    if (u < .4) p.line(previous.x, previous.y + 1, a.x, a.y + 1, C[2], fade);
                }
                previous = a;
            });
            if (t < CRACK) p.glow(HX + l + r, HY + r, 12, C[4], .4);
        }
        for (size_t i = 1; i < tipTrail.size(); ++i) {
            const double k = 1 - (t - tipTrail[i].time) / .06;
            const auto   a = tipTrail[i - 1].point, b = tipTrail[i].point;
            p.line(a.x, a.y, b.x, b.y, k > .6 ? WHITE : C[5], k);
        }
        const double q = t - CRACK;
        if (q >= 0 && q < .035)
            melee::star(p, HX + L, HY - 1, spikes, 1, 1, WHITE, C[5], C[6], C[4]);
        else if (q >= .035 && q < .07)
            melee::star(p, HX + L, HY - 1, spikes, 1.3, .5, C[6], C[5], C[5], C[3]);
        if (q >= .02 && q < .2) {
            const double k = (q - .02) / .18, r = 4 + 16 * ease::outCubic(k), th = std::max(1., 2.2 * (1 - k));
            const auto   color = k < .25 ? WHITE : k < .5 ? C[5] : k < .75 ? C[4] : C[3];
            for (double a = 0; a < TAU; a += .6 / r)
                for (double off = 0; off < th; off += .5)
                    p.dot(HX + L + std::cos(a) * (r - off), HY - 1 + std::sin(a) * (r - off) * .8, color, 1.15 - k);
            p.glow(HX + L, HY - 1, r + 6, C[4], .25 * (1 - k));
        }
        if (q >= .04 && q < .16) {
            const double k = (q - .04) / .12;
            for (const auto& s : slivers)
                melee::sliver(p, HX + L, HY - 1, s.angle, s.radius + s.speed * (q - .04), s.arc * (1 - .4 * k),
                              k < .4 ? C[6] : C[5], 1 - k);
        }
        if (q >= 0 && q < .15) p.glow(HX + L, HY - 1, 26, C[5], .7 * std::exp(-q / .04));
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& whipCrackDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks},
                                                {"length", 1, 512, false, 0, &PixelFxMeleeSettings::length}}};
    static const Descriptor            d{
        {PixelFxKind::WhipCrack, "melee/whip-crack", 192, 80, 30, 68, .85, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a1a10), hex(0x5c3a1e), hex(0x9c6a34), hex(0xd8a45a),
                              hex(0xffd890), hex(0xfff4d6), WHITE};
            s.melee.sparks = {HOT.begin(), HOT.end()};
            s.melee.length = 76;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<WhipCrack>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
