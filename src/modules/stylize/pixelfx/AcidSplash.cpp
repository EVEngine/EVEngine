// eve-hot-path
// Native adaptation of pixelfx nature/acid-splash.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureSimulation.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double HIT = .3, Y0 = -62, V0 = 60, SPREAD = .4, DRY = .7, GONE = 2.2, W = 30, PIT = 5, D = 2.8;
struct Spike {
    double angle, height, radius;
    bool   shed = false;
};
struct Stain {
    double x, t0, width;
};
struct Wisp {
    double x, y, t0, life, rise, length, amp, k, phase, drift;
};
struct Pop {
    double x, y, t0;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
};
template <class Painter>
void wisp(Painter& p, const Wisp& q, double t, uint32_t color) {
    const double age = t - q.t0, u = age / q.life;
    if (u < 0 || u >= 1) return;
    const double head = q.rise * age, tail = std::max(0., head - q.length),
                 fade = smooth(0, .15, u) * (1 - smooth(.4, 1, u));
    for (double h = tail; h <= head; h += .5) {
        const double s = (h - tail) / std::max(1., head - tail),
                     x = q.x + q.amp * std::sin(h * q.k + q.phase) * (.2 + s) + q.drift * age;
        p.blend(x, q.y - h, color, fade * std::sin(PI * s) * .42);
    }
}
struct AcidSplash final : Kernel {
    const std::vector<uint32_t> C;
    const uint32_t              scorch, burnt, steamColor;
    Rng                         R;
    Particles                   ps;
    nature::Drops               drops;
    Lights                      lights;
    std::vector<Stain>          stains;
    std::vector<Wisp>           wisps;
    std::vector<Pop>            pops;
    std::array<Spike, 13>       spikes{};
    std::array<double, 80>      rag{};
    double                      t   = 0;
    bool                        hit = false;
    explicit AcidSplash(const PixelFxSettings& o)
        : C(o.palette),
          scorch(mix(C[0], hex(0x140f0c), .6)),
          burnt(mix(C[1], hex(0x2a1a10), .6)),
          steamColor(mix(C[7], hex(0xe8ece6), .75)),
          R(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        for (size_t i = 0; i < spikes.size(); ++i)
            spikes[i] = {(double(i) / 13) * TAU + R.range(-.18, .18), R.range(4, 10), R.range(.85, 1.12), false};
        for (size_t i = 0; i < rag.size(); ++i) rag[i] = rand({uint32_t(i), o.seed, 5u}) - .5;
    }
    double pitWidth() const { return W * ease::outCubic(clamp((t - HIT) / SPREAD)) * 1.02; }
    double pitDepth() const { return PIT * ease::outCubic(clamp((t - HIT) / 1.1)); }
    double depth(double x) const {
        const double d = std::abs(x) / std::max(1., pitWidth());
        return d >= 1 ? -1 : pitDepth() * std::sqrt(1 - d * d) + rag[size_t((int(jsRound(x)) + 200) % 80)] * .9;
    }
    double level() const { return t < HIT ? 0 : -1.6 + (PIT + 1.6) * ease::inQuad(smooth(DRY, GONE, t)); }
    double width() const {
        const double L = level(), w = pitWidth();
        return L <= 0 ? w : w * std::sqrt(std::max(0., 1 - std::pow(L / std::max(.1, pitDepth()), 2)));
    }
    double heat() const { return smooth(HIT, HIT + .3, t) * (1 - smooth(GONE - .3, GONE + .25, t)); }
    double gobY() const { return Y0 + V0 * t + .5 * 950 * t * t; }
    double crownRadius() const { return 4 + 11 * ease::outCubic(clamp((t - HIT) / .16)); }
    double crownHeight() const { return 5 * std::sin(PI * clamp((t - HIT) / .26)); }
    double spikeK() const {
        const double u = clamp((t - HIT - .015) / .26);
        return std::sin(PI * std::pow(u, .6));
    }
    std::array<double, 4> tipOf(const Spike& s) const {
        const double r = crownRadius() * s.radius, h = crownHeight(), sn = std::sin(s.angle), sh = s.height * spikeK(),
                     bx = std::cos(s.angle) * r, by = sn * r * .3 - h * (.75 + .25 * std::abs(sn));
        return {bx, by, bx + std::cos(s.angle) * sh * .55, by - sh};
    }
    void steam(double x, double y, bool big) {
        wisps.push_back({x, y, t, std::min(R.range(.5, .9) * (big ? 1.3 : 1), D - .05 - t), R.range(22, 32),
                         R.range(5, 10) * (big ? 1.4 : 1), R.range(1, 2.5), R.range(.35, .6), R.range(0, TAU),
                         R.range(-4, 4)});
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life); }
    void crown(Paint& p, bool back) const {
        if (t < HIT || t > HIT + .27) return;
        const double r = crownRadius(), h = crownHeight(), ry = r * .3;
        for (int x = int(std::floor(-r)); x <= std::ceil(r); ++x) {
            const double c = (x + .5) / r;
            if (std::abs(c) > 1) continue;
            const double e = std::sqrt(1 - c * c), yb = (back ? -ry : ry) * e, hh = h * (.75 + .25 * e);
            for (int y = int(std::floor(yb - hh)); y <= jsRound(yb); ++y) {
                if (y == std::floor(yb - hh))
                    p.px(x, y, back ? C[5] : c < .1 ? C[7] : C[6]);
                else
                    p.blend(x, y, C[size_t((c < -.25 ? 5 : c > .45 ? 3 : 4) - (back ? 1 : 0))], back ? .55 : .7);
            }
        }
        for (const auto& s : spikes) {
            if ((std::sin(s.angle) < 0) != back) continue;
            const auto q = tipOf(s);
            p.line(q[0], q[1], q[2], q[3], back ? C[4] : C[6]);
            p.line(q[0] + 1, q[1], (q[0] + q[2]) / 2 + .5, (q[1] + q[3]) / 2, back ? C[3] : C[5]);
            if (!s.shed) {
                p.px(q[2], q[3] - 1, back ? C[5] : C[7]);
                p.px(q[2] + 1, q[3] - 1, C[5]);
            }
        }
    }
    void update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(0, -4, 28, WHITE, .6, .08);
            lights.add(0, -10, 56, C[5], .5, .6);
            for (int i = 0; i < 26; ++i) {
                const double a = -PI / 2 + R.range(-1.35, 1.35), sp = R.range(60, 250);
                drops.add({R.range(-3, 3), -2, std::cos(a) * sp, std::sin(a) * sp, R.isChance(.3) ? 2. : 1.});
            }
            for (int i = 0; i < 10; ++i) {
                const double a = -PI / 2 + R.range(-1.2, 1.2), sp = R.range(120, 260);
                emit({ParticleKind::Spark, 0, -2, std::cos(a) * sp, std::sin(a) * sp, R.range(.12, .3)});
            }
            for (int i = 0; i < 5; ++i) {
                const double x = R.range(-10, 10), y = -R.range(2, 6);
                steam(x, y, true);
            }
        }
        if (hit)
            for (auto& s : spikes)
                if (!s.shed && t > HIT + .09) {
                    s.shed         = true;
                    const auto tip = tipOf(s);
                    drops.add({tip[2], tip[3] - 1, std::cos(s.angle) * R.range(30, 80), -R.range(50, 130),
                               R.isChance(.4) ? 2. : 1.});
                }
        const auto floor = [](double) { return 0.; };
        drops.update(dt, floor, [&](const nature::Drop& d, double) {
            if (std::abs(d.x) > pitWidth()) stains.push_back({d.x, t, d.size * .8 + R.range(.3, 1.2)});
            if (t < GONE) steam(d.x, -1, false);
        });
        const double lv = level(), w2 = width();
        if (t > HIT + .15 && t < GONE && w2 > 2) {
            if (R.isChance(.3 * f)) {
                const double x = R.range(-.85, .85) * w2, y = std::min(lv, depth(x)) - 1;
                pops.push_back({x, y, t});
                // Source samples R.int in EVERY loop-condition evaluation, including the final check.
                for (int j = 0; j < R.integer(1, 3); ++j)
                    drops.add({x, std::min(y - 1, -1.), R.range(-40, 40), -R.range(50, 110), 1});
            }
            if (R.isChance(.14 * f)) {
                const double x = R.range(-.9, .9) * w2, y = std::min(lv, 0.) - 1;
                const bool   big = R.isChance(.25);
                steam(x, y, big);
            }
        }
        if (t > GONE - .2 && t < D - .5 && R.isChance(.08 * f)) {
            const double x = R.range(-.7, .7) * pitWidth();
            steam(x, 0, false);
        }
        std::erase_if(pops, [&](const auto& p) { return t - p.t0 > .1; });
        std::erase_if(wisps, [&](const auto& q) { return t - q.t0 > q.life; });
        ps.update(dt, R, floor, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double fade = 1 - smooth(D - .6, D - .08, t), hk = heat(), w = pitWidth(), lv = level();
        for (const auto& s : stains) {
            const double age = t - s.t0, k = 1 - smooth(1, 1.8, age);
            const auto   c = age < .15 ? C[7] : age < .5 ? C[6] : age < .9 ? C[4] : burnt;
            for (int x = int(std::floor(s.x - s.width)); x <= std::ceil(s.x + s.width); ++x)
                p.dot(x, 0, std::abs(x - s.x) < s.width * .5 ? scorch : c, k * fade);
            if (age < .5) p.glow(s.x, -1, 5, C[5], .3 * (1 - age / .5));
        }
        crown(p, true);
        if (t > HIT && w > 1) {
            for (int x = int(std::floor(-w)) - 1; x <= std::ceil(w) + 1; ++x) {
                const double dp = depth(x + .5);
                if (dp < 0) continue;
                const int  bottom = std::max(0, int(jsRound(dp)));
                const bool edge   = std::abs(x + .5) / w > .88;
                for (int y = 0; y <= bottom; ++y)
                    p.dot(x, y, y == bottom ? (hk > .5 ? C[6] : hk > .15 ? C[4] : burnt) : scorch, fade);
                p.dot(x, bottom + 1, hk > .5 ? C[5] : hk > .15 ? C[3] : scorch, fade * .8);
                if (edge) p.dot(x, -1, hk > .4 ? C[7] : hk > .1 ? C[5] : burnt, fade);
            }
            for (int side : {-1, 1}) p.glow(side * w, -1, 8, C[6], .45 * hk);
            if (t < GONE)
                for (int x = int(std::floor(-w)); x <= std::ceil(w); ++x) {
                    const double dp = depth(x + .5);
                    if (dp < 0) continue;
                    const double d      = std::abs(x + .5) / w,
                                 dome   = lv < 0 ? jsRound(lv * std::sqrt(std::max(0., 1 - d * d))) : jsRound(lv),
                                 bottom = jsRound(dp);
                    if (dome > bottom) continue;
                    const double n = noise(x * .25 - t * 2.4, 3) + noise(x * .1 + t * 1.4, 5) * .6;
                    for (int y = int(dome); y <= bottom; ++y)
                        p.px(x, y,
                             y == dome       ? (n > .7 ? C[7] : C[6])
                             : y == dome + 1 ? (n > .2 ? C[5] : C[4])
                             : y >= bottom   ? C[3]
                                             : C[4]);
                }
            p.glow(0, -6, w * 1.4 + 10, C[4], .3 * hk * (.85 + .15 * std::sin(t * 9)));
            p.glow(0, 1, w * .9, C[6], .2 * hk);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        if (t < HIT) {
            const double y = gobY(), v = V0 + 950 * t, st = 1 + v * .0022, rx = 2.6 / std::sqrt(st), ry = 2.8 * st;
            p.shader(-rx - 1, y - ry - 4, rx + 1, y + ry + 1, [&](double x, double yy, ShadeOutput& out) {
                const double dx = x / rx, dy = (yy - y) / ry;
                const bool   tail = dy < 0 && std::abs(x) < rx * (1 + dy * .9) + .4 && dy > -1.7;
                const double d    = dx * dx + dy * dy;
                if (d > 1.25 && !tail) return;
                out.c = d > 1 && !tail ? C[2] : dx < -.2 && dy < -.1 ? C[7] : dy > .45 ? C[4] : C[5];
            });
            p.px(-1, y - ry * .3, WHITE);
            for (int i = 1; i <= 3; ++i) p.dot(0, y - ry - 2 - i * 4, C[5], 1 - i * .25);
            p.glow(0, y, 10, C[5], .45);
        }
        crown(p, false);
        const double e = t - HIT;
        if (e >= 0 && e < .07) {
            const double k = 1 - e / .07;
            p.ellipse(0, -2, 6 + 6 * (1 - k), 2.5, k > .5 ? WHITE : C[7]);
            p.sparkle(0, -4, 8 * k, C[7], C[6], .6);
        }
        for (const auto& q : pops) {
            const double k = (t - q.t0) / .1;
            p.px(q.x, q.y, WHITE);
            for (const auto d : std::array<Point, 3>{{{1, 0}, {-1, 0}, {0, -1}}})
                p.dot(q.x + d.x * (1 + k * 2), q.y + d.y * (1 + k * 2), C[7], 1 - k);
            p.glow(q.x, q.y, 4, C[6], .4 * (1 - k));
        }
        for (const auto& q : wisps) wisp(p, q, t, steamColor);
        drops.draw(p, C);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& acidSplashDescriptor() {
    static const Descriptor d{
        {PixelFxKind::AcidSplash, "nature/acid-splash", 184, 116, 92, 100, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 6;
            s.palette = {hex(0x0e140a), hex(0x1c3010), hex(0x2c5214), hex(0x457e18),
                         hex(0x6aae1c), hex(0xa0dc2a), hex(0xdcff6a), hex(0xf8ffd0)};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<AcidSplash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
