// eve-hot-path
// Native adaptation of pixelfx spawn/soul-rise.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/SpawnBody.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double LEAVE = .35, TOP = 2.05, GONE = 2.55, D = 3.2;
const uint32_t   SHADOW = hex(0x0c0f1e);
struct SoulEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size;
};
struct SoulRise final : Kernel {
    const std::vector<uint32_t>        C;
    Rng                                R;
    const spawn::Body                  body;
    const std::vector<spawn::RimPixel> rim;
    const double                       height;
    const int                          waist;
    std::vector<int8_t>                tones;
    Particles                          ps;
    Lights                             lights;
    double                             t    = 0;
    bool                               left = false;
    explicit SoulRise(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          body(spawn::bodyOf(o.actor, C)),
          rim(spawn::rimOf(body)),
          height(o.spawn.height),
          waist(int(jsRound(body.height * .6))),
          tones(size_t(body.width) * size_t(body.height), -1) {
        for (const auto& b : body.list)
            tones[size_t(b.my * body.width + b.mx)] =
                int8_t(b.edge ? 6 : 3 + std::min(2., std::floor(luma(b.color) * 3.6)));
        ps.sparks = {WHITE, C[5], C[4], C[3]};
    }
    double lift() const { return height * ease::inOutSine(clamp((t - LEAVE) / (TOP - LEAVE))); }
    double stretch() const { return 1 + .22 * ease::inOutSine(clamp((t - LEAVE) / (TOP - LEAVE))); }
    double sway(double row) const { return std::sin(t * 3 - row * .12) * 2.2 * smooth(LEAVE, LEAVE + .5, t); }
    double melt() const { return smooth(LEAVE + .1, LEAVE + .9, t); }
    double thin() const { return smooth(TOP - .25, GONE, t); }
    void   emit(const SoulEmission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    const spawn::RimPixel& pickRim() { return rim[size_t(std::floor(R.next() * double(rim.size())))]; }
    void                   update(double dt) override {
        t += dt;
        if (!left && t >= LEAVE) {
            left = true;
            lights.add(0, -20, 50, C[4], .85, .55);
            lights.add(0, -20, 24, WHITE, .8, .12);
            for (int i = 0; i < 10; ++i) {
                const auto& c = pickRim();
                emit({ParticleKind::Star, c.x, c.y, R.range(-20, 20), -R.range(20, 60), R.range(.4, .8),
                      R.pick<uint32_t>(std::array{WHITE, C[5]}), R.range(.8, 1.8)});
            }
        }
        if (t > LEAVE && t < GONE) {
            const double s = stretch(), y0 = body.y * s - lift(), th = thin();
            for (int n = 0; n < (th > 0 ? 3 : 1); ++n) {
                if (!R.isChance(th > 0 ? .9 : .5)) continue;
                const auto& c   = pickRim();
                const int   row = c.my;
                if (double(row) / body.height < 1 - th * 1.1 - .05 && th > 0) continue;
                const double x = c.x * (1 - .12 * (s - 1) * 4) + sway(row), y = y0 + c.my * s;
                const double life = std::min({R.range(.5, 1.1), D - .1 - t, (y + 120) / 60});
                if (life > .1)
                    emit({R.isChance(.7) ? ParticleKind::Mote : ParticleKind::Star, x, y, R.range(-12, 12),
                          -R.range(25, 60), life, R.pick<uint32_t>(std::array{WHITE, C[5], C[5], C[4]}),
                          R.range(.5, 1)});
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void ring(Paint& p, bool near) const {
        const double k = (t - LEAVE) / .5;
        if (k > 0 && k < 1)
            band(
                p, 0, 0, 6 + 26 * ease::outCubic(k), .18, std::max(1., 3 * (1 - k)), 1.15 - k, C,
                [&](double g) { return (std::sin(g) >= 0) == near; }, k < .12);
    }
    void drawBack(Paint& p) const override {
        const double k = smooth(0, LEAVE, t) * (1 - smooth(LEAVE + .4, TOP, t));
        if (k > 0) p.glow(0, -1, 26, C[4], .3 * k);
        ring(p, false);
    }
    void draw(Paint& p) const override {
        if (t < LEAVE + .3) {
            const double k = t < LEAVE ? t / LEAVE : 1 - (t - LEAVE) / .3;
            for (const auto& c : rim)
                if (rand({jsUint(c.mx), jsUint(c.my), jsUint(std::floor(t * 30))}) < .25 + .7 * k)
                    p.px(c.x, c.y, k > .7 ? WHITE : C[5]);
            p.glow(0, -19, 26, C[4], .45 * k);
        }
        const double dim = smooth(LEAVE, LEAVE + .6, t) * (1 - smooth(GONE, D - .1, t));
        if (dim > 0)
            for (const auto& b : body.list) p.blend(b.x, b.y, SHADOW, .45 * dim);
        ring(p, true);
        if (t < LEAVE || t > GONE + .1) {
            ps.draw(p);
            lights.draw(p);
            return;
        }
        const double s = stretch(), L = lift(), m = melt(), th = thin(), top = body.y * s - L,
                     sx   = 1 - .12 * (s - 1) * 4;
        const double tail = jsRound(10 * m), y1 = top + (body.height + tail) * s, fadeIn = clamp((t - LEAVE) / .15);
        for (int y = int(std::floor(top)); y <= std::ceil(y1); ++y) {
            const int my = int(std::floor((y + .5 - top) / s));
            if (my < 0) continue;
            const double k      = my >= waist ? double(my - waist + 1) / (body.height - waist + tail) : 0,
                         narrow = 1 - k * m * .95;  // A one-row source body can make the extra tail row NaN; its JS
                                                    // x-loop performs no writes.
            if (!std::isfinite(narrow) || narrow <= .05) continue;
            const double cx = sway(my) + std::sin(my * .7 - t * 8) * 2 * k * m,
                         a  = (my >= waist ? 1 - k * .45 : 1) * fadeIn;
            for (int x = int(std::floor(cx - double(body.width) / 2 - 4));
                 x <= std::ceil(cx + double(body.width) / 2 + 4); ++x) {
                const int mx = int(std::floor((x + .5 - cx) / (sx * narrow) - body.x));
                if (th > 0 && my / (body.height + tail) > 1 - th * 1.25 + .25 * rand({jsUint(mx), jsUint(my), 5u}) - .1)
                    continue;
                int ti;
                if (my < body.height) {
                    if (mx < 0 || mx >= body.width) continue;
                    ti = tones[size_t(my * body.width + mx)];
                    if (ti < 0) continue;
                } else {
                    const double w = (1 - (my - body.height) / std::max(1., tail)) * 3.2 * m, d = std::abs(x + .5 - cx);
                    if (d > w) continue;
                    ti = d > w - 1 ? 5 : 4;
                }
                if (t < LEAVE + .07)
                    p.px(x, y, ti >= 6 ? C[5] : WHITE);
                else if (ti >= 5)
                    p.dot(x, y, ti >= 6 ? WHITE : C[5], std::min(1., a * 1.3));
                else
                    p.blend(x, y, C[size_t(ti)], a * .7);
            }
        }
        const double cy = top + body.height * s * .45;
        p.glow(sway(10), cy, 30, C[4], .42 * fadeIn * (1 - th));
        p.glow(sway(10), cy - 6, 14, C[5], .3 * fadeIn * (1 - th));
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& soulRiseDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"height", 0, 256, false, 0, &PixelFxSpawnSettings::height}}};
    static const Descriptor            d{
        {PixelFxKind::SoulRise, "spawn/soul-rise", 96, 140, 48, 126, D, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 27;
            s.actor.source = PixelFxActorSource::Dummy;
            s.spawn.height = 50;
            s.palette      = {hex(0x101a33), hex(0x1f3563), hex(0x35609e), hex(0x5c96d4),
                              hex(0x94c8f0), hex(0xd2f0ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SoulRise>(s); },
        nullptr,
        nullptr,
        true,
        1};
    return d;
}
}  // namespace eve::stylize::pixelfx
