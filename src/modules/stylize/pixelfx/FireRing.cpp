// eve-hot-path
// Adapted from pixelfx fire/fire-ring.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                     SY = .2, CHARGE = .32, SPREAD = .75;
const uint32_t                       SOOT = hex(0x0d0a12), SOOT2 = hex(0x17121d), WARM = rgb(255, 176, 96);
constexpr std::array<fire::Layer, 3> EMBER_LAYERS{{{1, 1, 7}, {.7, .8, 6}, {.4, .55, 5}}};
struct Site {
    double   x, y;
    bool     back;
    double   t0, life, height, width;
    uint32_t n;
};
struct Spawn {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct FireRing final : Kernel {
    std::array<uint32_t, 9> H{};
    double                  reach, t = 0;
    Rng                     rng;
    uint32_t                S, glow;
    Particles               ps;
    Lights                  lights;
    std::vector<Site>       sites;
    bool                    burst = false;
    explicit FireRing(const PixelFxSettings& o) : reach(o.fire.reach), rng(o.seed), S(o.seed * 7) {
        std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
        ps.sparks = {H[1], H[2], H[4], H[5]};
        glow      = mix(mix(H[3], H[4], .5), WHITE, .25);
        for (double r = 10; r <= reach; r += 9) {
            const double per = TAU * std::sqrt((r * r + (r * SY) * (r * SY)) / 2);
            const int    n   = std::max(5, int(jsRound(per / 8.5)));
            for (int k = 0; k < n; ++k) {
                const double a = (double(k) + rng.range(.15, .85)) / n * TAU, rr = r + rng.range(-3, 3),
                             q = std::min(1., rr / reach);
                sites.push_back({std::cos(a) * rr, std::sin(a) * rr * SY, std::sin(a) < 0,
                                 reachedAt(rr) + rng.range(0, .04), (.3 + q * q) * rng.range(.85, 1.2),
                                 rng.range(9, 15) * (.6 + .55 * q), rng.range(1.8, 2.7) * (.75 + .3 * q),
                                 uint32_t(rng.integer(0, 100000))});
            }
        }
        std::stable_sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.y < b.y; });
    }
    double front() const { return reach * ease::outCubic(clamp((t - CHARGE) / SPREAD)); }
    double reachedAt(double d) const { return CHARGE + SPREAD * (1 - std::cbrt(1 - std::min(1., d / reach))); }
    void   emit(Spawn p) { ps.spawn(rng, p.kind, p.x, p.y, p.vx, p.vy, p.life, p.color, p.size, p.delay, p.flags); }
    void   update(double dt) override {
        t += dt;
        if (t < CHARGE - .05 && rng.isChance(.9 * dt * 60)) {
            const double a = rng.range(0, TAU), d = rng.range(26, 54), T = rng.range(.18, .26),
                         v = d * 3 / (std::exp(3 * T) - 1), x = std::cos(a) * d,
                         y = std::min(-2., -6 + std::sin(a) * d * .45), length = jsHypot2(x, y + 4);
            const double len = length == 0 ? 1 : length;
            emit({ParticleKind::Streak, x, y, -x / len * v, (-4 - y) / len * v, T, rng.isChance(.5) ? H[2] : H[3]});
        }
        if (t < CHARGE && rng.isChance(.5 * dt * 60 * (t / CHARGE)))
            emit({ParticleKind::Fire, rng.range(-4, 4), -1, rng.range(-8, 8), -rng.range(20, 40), rng.range(.12, .24),
                  0, rng.range(1, 2.2)});
        if (!burst && t >= CHARGE) {
            burst = true;
            lights.add(0, -18, 64, WARM, .85, .5);
            lights.add(0, -6, 30, WHITE, .8, .08);
            for (int i = 0; i < 40; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(80, 220);
                emit({ParticleKind::Spark, std::cos(a) * 3, -2, std::cos(a) * sp,
                      std::sin(a) * sp * .4 - rng.range(40, 120), rng.range(.2, .5)});
            }
        }
        if (t > CHARGE && t < CHARGE + SPREAD + 1.2)
            for (int i = 0; i < 3; ++i) {
                if (!rng.isChance(.75 * dt * 60)) continue;
                const auto   s   = rng.pick<Site>(sites);
                const double age = t - s.t0;
                if (age < 0 || age > s.life * .7) continue;
                const auto back = s.back ? ParticleFlag::Back : ParticleFlag::None;
                emit({ParticleKind::Fire, s.x + rng.range(-1, 1), s.y - s.height * .6, rng.range(-5, 5),
                      -rng.range(20, 40), rng.range(.2, .4), 0, rng.range(1.2, 2.4), 0,
                      rng.isChance(.5) ? ParticleFlag::Smokes | back : back});
                if (rng.isChance(.12))
                    emit({ParticleKind::Ember, s.x, s.y - s.height * .5, rng.range(-15, 15), -rng.range(30, 70),
                          rng.range(.6, 1.3), H[3], 1, 0, back});
            }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void flames(Paint& p, bool back) const {
        const fire::Noise N{0, t};
        for (const auto& s : sites) {
            if (s.back != back) continue;
            const double age = t - s.t0;
            if (age < 0 || age > s.life) continue;
            const double u = age / s.life, rise = ease::outBack(clamp(age / .08), 2),
                         fade = 1 - std::pow(std::max(0., (u - .1) / .9), 1.3);
            const double h    = s.height * rise * fade * (.85 + .15 * noise(t * 3.3, s.n)) *
                                (1 + .5 * std::max(0., noise(t * 2.6, s.n + 70) - .55));
            if (h < 1) continue;
            const auto layers = u < .3    ? (back ? std::span<const fire::Layer>(fire::BACK_LAYERS)
                                                  : std::span<const fire::Layer>(fire::LAYERS))
                                : u < .65 ? std::span<const fire::Layer>(fire::BACK_LAYERS)
                                          : std::span<const fire::Layer>(EMBER_LAYERS);
            fire::flameTongue(p, s.x, s.y, h, s.width * (.6 + .4 * fade), N(2.2, s.n + 40) * 1.5 - s.x * .02, N, s.n, H,
                              layers);
        }
    }
    void scorch(Paint& p) const {
        const double fr = front(), end = 1 - smooth(2.7, 3.75, t);
        if (fr < 1 || end <= 0) return;
        for (int y = int(std::floor(-fr * SY)); y <= int(std::ceil(fr * SY)); ++y)
            for (int x = int(std::floor(-fr)); x <= int(std::ceil(fr)); ++x) {
                const double d = jsHypot2(x + .5, (y + .5) / SY);
                if (d > fr) continue;
                const double since = t - reachedAt(d), h = rand({jsUint(x), jsUint(y), S}),
                             hot = 1 - since / (1.2 + h * .9);
                if (hot > 0 && h < .03 + .06 * hot) {
                    p.px(x, y, hot > .7 ? H[3] : hot > .45 ? H[4] : hot > .2 ? H[5] : H[6]);
                    p.light(x, y, H[4], .3 * hot);
                    continue;
                }
                p.blend(x, y, h < .5 ? SOOT : SOOT2, std::min(1., since * 3) * (.55 - d / reach * .25) * end);
            }
    }
    void ring(Paint& p, bool back) const {
        const double k = (t - CHARGE) / (SPREAD + .1);
        if (k < 0 || k >= 1) return;
        const std::array<uint32_t, 4> ramp{H[6], H[4], H[2], H[1]};
        band(
            p, 0, 0, front() + 1.5, SY, std::max(1.5, 6 * (1 - k)), k < .75 ? 1 : (1 - k) / .25, ramp,
            [&](double g) { return (std::sin(g) < 0) == back; }, k < .12);
    }
    void drawBack(Paint& p) const override {
        scorch(p);
        ring(p, true);
        flames(p, true);
        fire::simDraw(p, ps, H, Paint::Layer::Back);
        const double fr = front(), k = t < CHARGE ? 0 : 1 - smooth(CHARGE + SPREAD, CHARGE + SPREAD + 1.4, t);
        if (k > 0)
            for (int i = 0; i < 16; ++i)
                p.glow(std::cos(double(i) / 16 * TAU) * fr, std::sin(double(i) / 16 * TAU) * fr * SY - 3, 16, glow,
                       .2 * k);
    }
    void draw(Paint& p) const override {
        ring(p, false);
        flames(p, false);
        fire::simDraw(p, ps, H, Paint::Layer::Front);
        if (t < CHARGE) p.glow(0, -3, 8 + 22 * (t / CHARGE), glow, .6 * (t / CHARGE));
        if (t >= CHARGE && t < CHARGE + .1) {
            const double k = 1 - (t - CHARGE) / .1;
            p.ellipse(0, -1, 14 * (.6 + .4 * k), 4, H[2]);
            p.ellipse(0, -1, 9 * (.6 + .4 * k), 2.6, k > .5 ? WHITE : H[1]);
        }
        const double k = smooth(CHARGE - .05, CHARGE, t) * (1 - smooth(CHARGE + SPREAD * .5, CHARGE + SPREAD + 1.2, t));
        if (k > 0) p.glow(0, -6, 92, glow, .3 * k);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& fireRingDescriptor() {
    static const std::array<Option, 1> options{{{"reach", 20, 200, false, 0, &PixelFxFireSettings::reach}}};
    static const Descriptor            descriptor{
        {PixelFxKind::FireRing, "fire/fire-ring", 224, 128, 112, 92, 3.9, false},
        9,
        [](PixelFxSettings& s) {
            fireDefaults(s, PixelFxKind::Torch);
            s.seed       = 6;
            s.fire.reach = 92;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<FireRing>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
