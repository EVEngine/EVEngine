// eve-hot-path
// Native adaptation of pixelfx spawn/disintegrate.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/SpawnBody.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double FLASH = .42, WHITE_T = .07, P0 = FLASH + WHITE_T, PEEL = 1.2, CORE_Y = -20, D = 3.4;
struct PeelCell {
    double key;
    bool   gone = false;
};
struct Mote {
    double                  x, y, vx, vy, life, max;
    uint32_t                color;
    int                     kind;
    double                  lift;
    std::array<uint32_t, 4> cool;
    bool                    own, rest;
    double                  seed;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct Disintegrate final : Kernel {
    const std::vector<uint32_t>                  C, A;
    Rng                                          R;
    const double                                 W;
    const std::array<std::array<uint32_t, 4>, 3> COOL;
    const spawn::Body                            body;
    const std::vector<spawn::RimPixel>           rim;
    std::vector<PeelCell>                        cells;
    Particles                                    ps;
    Lights                                       lights;
    std::vector<Mote>                            motes;
    double                                       t       = 0;
    bool                                         flashed = false;
    explicit Disintegrate(const PixelFxSettings& o)
        : C(o.palette),
          A(o.spawn.accent),
          R(o.seed),
          W(o.spawn.wind),
          COOL{{{{C[6], C[5], C[5], C[4]}}, {{A[2], A[1], A[1], A[0]}}, {{WHITE, C[6], C[5], C[5]}}}},
          body(spawn::bodyOf(o.actor, C)),
          rim(spawn::rimOf(body)) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        cells.reserve(body.list.size());
        for (const auto& b : body.list) {
            const double u = (b.y - body.y) / body.height;
            cells.push_back({u + .14 * (rand({jsUint(b.mx), jsUint(b.my), 3u}) - .5) + .05 * noise(b.mx * .45, 7u) -
                                 (b.edge ? .03 : 0),
                             false});
        }
    }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < FLASH - .1 ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden;
    }
    double front(double time) const { return -.06 + 1.14 * ease::inOutSine(clamp((time - P0) / PEEL)); }
    double gust(double time) const { return .7 + .5 * (noise(time * 1.6, 11u) * .5 + .5); }
    void   emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    std::array<uint32_t, 4> sampleCool() { return COOL[R.isChance(.3) ? 1 : R.isChance(.3) ? 2 : 0]; }
    void                    letGo(size_t i) {
        cells[i].gone    = true;
        const auto& b    = body.list[i];
        const bool  lo   = b.y > -9 && R.isChance(.22);
        const int   kind = lo ? 2 : R.isChance(.42) ? 1 : R.isChance(.07) ? 3 : 0;
        if (kind == 3) {
            emit({ParticleKind::Star, b.x, b.y, R.range(-6, 14) * W, R.range(-26, -8), R.range(.6, 1.1),
                  R.isChance(.5) ? C[6] : WHITE, R.range(1, 2.2)});
            return;
        }
        const double life = kind == 2 ? R.range(.7, 1.2) : kind == 1 ? R.range(.25, .5) : R.range(.9, 1.7);
        const double out  = (b.x + .5) * R.range(2, 6);
        motes.push_back({b.x + .5, b.y + .5, (out + R.range(-6, 16)) * (kind == 2 ? .4 : 1.),
                         kind == 2 ? R.range(-30, -8) : R.range(-46, -8), life, life, b.color, kind, R.range(.4, 1.4),
                         sampleCool(), R.isChance(.2), false, R.range(0, 100)});
    }
    void update(double dt) override {
        t += dt;
        if (t < FLASH - .05 && R.isChance(.55)) {
            const double a = R.range(0, TAU), distance = R.range(22, 44), life = R.range(.16, .26),
                         v = (distance * 3) / (std::exp(3 * life) - 1);
            const double x = std::cos(a) * distance, y = std::min(CORE_Y + std::sin(a) * distance * .9, -3.),
                         raw = jsHypot2(x, y - CORE_Y), l = raw == 0 ? 1 : raw;
            emit(
                {ParticleKind::Streak, x, y, (-x / l) * v, ((CORE_Y - y) / l) * v, life, R.isChance(.5) ? C[6] : C[5]});
        }
        if (!flashed && t >= FLASH) {
            flashed = true;
            lights.add(0, CORE_Y, 64, C[4], .85, .6);
            lights.add(0, CORE_Y, 30, WHITE, .8, .14);
            for (int i = 0; i < 14; ++i) {
                const double a = R.range(0, TAU), sp = R.range(50, 150);
                emit({ParticleKind::Star, R.range(-6, 6), CORE_Y + R.range(-10, 10), std::cos(a) * sp,
                      std::sin(a) * sp * .7 - 25, R.range(.35, .8), R.pick<uint32_t>(std::array{WHITE, C[6], C[5]}),
                      R.range(.6, 1.8)});
            }
        }
        if (t >= P0) {
            const double f = front(t);
            for (size_t i = 0; i < cells.size(); ++i)
                if (!cells[i].gone && cells[i].key < f) letGo(i);
        }
        const double g = gust(t) * (1 - .55 * smooth(P0 + PEEL, P0 + PEEL + 1.2, t));
        for (size_t i = motes.size(); i > 0; --i) {
            auto& m = motes[i - 1];
            if ((m.life -= dt) <= 0) {
                motes.erase(motes.begin() + std::ptrdiff_t(i - 1));
                continue;
            }
            if (m.kind == 2) {
                if (m.rest) continue;
                m.vx += (W * 30 * g - m.vx * 1.2) * dt;
                m.vy += 260 * dt - m.vy * 1.2 * dt;
                m.x += m.vx * dt;
                const double ny = m.y + m.vy * dt;
                if (m.vy > 0 && ny >= -.5) {
                    if (m.vy > 50) {
                        m.vy *= -.35;
                        m.vx *= .6;
                        m.y = -.6;
                    } else {
                        m.rest = true;
                        m.y    = -.5;
                    }
                } else
                    m.y = ny;
                continue;
            }
            const double age = m.max - m.life, k = std::min(1., age / .15);
            const double fx   = W * (100 * g + 30 * noise2(m.x * .07, m.y * .07 - t * .8, 3u)) * k;
            const double fy   = (-30 * m.lift + 42 * noise2(m.x * .06 + 40, m.y * .06 - t * 1.1, 5u)) * k,
                         drag = m.kind == 1 ? 3. : 1.5;
            m.vx += (fx - m.vx * drag) * dt;
            m.vy += (fy - m.vy * drag) * dt;
            m.x += m.vx * dt;
            m.y += m.vy * dt;
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k = (t - FLASH) / .5;
        if (k > 0 && k < 1)
            band(
                p, 0, 0, 6 + 30 * ease::outCubic(k), .18, std::max(1., 3.5 * (1 - k)), 1.15 - k, C,
                [](double g) { return std::sin(g) < 0; }, k < .12);
        const double pool = smooth(.1, FLASH, t) * (1 - smooth(P0 + PEEL * .8, P0 + PEEL + .6, t));
        if (pool > 0) p.glow(0, -1, 30, C[4], .22 * pool);
    }
    void draw(Paint& p) const override {
        const double f = front(t);
        if (t < FLASH) {
            const double   k  = t / FLASH;
            const uint32_t fr = jsUint(std::floor(t * 30));
            for (const auto& c : rim) {
                const double h = rand({jsUint(c.mx * 61 + c.my), fr, 7u});
                if (h < .12 + .62 * k * k)
                    p.px(c.x, c.y,
                         k > .72 && rand({jsUint(c.mx), jsUint(c.my), fr}) < .5 ? WHITE
                         : h < .2                                               ? C[6]
                                                                                : C[5]);
            }
            p.glow(0, CORE_Y, 26, C[4], .42 * k);
            p.glow(0, CORE_Y, 12, C[6], .25 * k * k);
        }
        if (t >= FLASH - .1) {
            const double shake = t < FLASH ? (jsUint(std::floor(t * 60)) % 2 ? 1. : -1.) * .6 : 0;
            const bool   white = t >= FLASH && t < P0;
            for (size_t i = 0; i < cells.size(); ++i) {
                if (cells[i].gone) continue;
                const auto& b = body.list[i];
                uint32_t    c;
                if (white)
                    c = b.edge ? C[6] : WHITE;
                else if (t < FLASH)
                    c = b.edge ? mix(b.color, C[6], .7) : mix(b.color, C[5], .25);
                else {
                    const double d          = cells[i].key - f;
                    const auto   aboveIndex = body.cellIndex(b.mx, b.my - 1);
                    const bool   above      = !aboveIndex || cells[*aboveIndex].gone;
                    c                       = above && d < .06 ? WHITE
                                              : d < .03        ? mix(b.color, C[6], .7)
                                              : d < .1         ? mix(b.color, C[5], .4)
                                              : b.edge         ? mix(b.color, C[5], .5)
                                                               : mix(b.color, C[4], .16);
                }
                p.px(b.x + shake, b.y, c);
            }
            if (white) {
                p.glow(0, CORE_Y, 34, C[5], .7);
                p.sparkle(0, CORE_Y - 3, 9, C[6], C[4], .5);
            }
        }
        if (t >= P0 && f < 1.05) {
            const double fy = body.y + clamp(f) * body.height, k = 1 - smooth(.85, 1.05, f);
            p.glowLine(-10, fy, 10, fy, 8, C[5], .32 * k);
            p.glow(0, fy, 22, C[4], .2 * k);
        }
        const double k = (t - FLASH) / .5;
        if (k > 0 && k < 1)
            band(
                p, 0, 0, 6 + 30 * ease::outCubic(k), .18, std::max(1., 3.5 * (1 - k)), 1.15 - k, C,
                [](double g) { return std::sin(g) >= 0; }, k < .12);
        for (const auto& m : motes) {
            const double age = m.max - m.life, k2 = m.life / m.max, a = k2 < .35 ? k2 / .35 : 1.;
            uint32_t     c;
            if (age < .034)
                c = mix(m.color, WHITE, .55);
            else if (age < .06)
                c = WHITE;
            else if (age < .15)
                c = m.cool[0];
            else {
                const double   u  = (age - .15) / std::max(.05, m.max - .15);
                const uint32_t cc = m.cool[size_t(1 + std::min(2, int(std::floor(u * 3))))];
                c                 = m.own && u < .5 ? mix(m.color, cc, .55) : cc;
                if (m.kind == 0 && std::sin(t * 21 + m.seed) > .8) c = m.cool[0];
            }
            if (m.kind == 2) {
                p.dot(m.x, m.y, c, a);
                p.glow(m.x, m.y, 3, C[5], .3 * k2);
                continue;
            }
            const double sp = jsHypot2(m.vx, m.vy), denom = sp == 0 ? 1 : sp, ux = m.vx / denom, uy = m.vy / denom;
            if (sp > 30 && m.kind == 0) p.dot(m.x - ux * 1.2, m.y - uy * 1.2, scale(c, .6), a * .85);
            if (sp > 85 && m.kind == 0) p.dot(m.x - ux * 2.4, m.y - uy * 2.4, scale(c, .4), a * .6);
            p.dot(m.x, m.y, c, a);
            if (k2 > .15) p.glow(m.x, m.y, 2.5, c, (m.kind == 1 ? .16 : .11) * k2);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& disintegrateDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"accent", 1, 64, false, 3, &PixelFxSpawnSettings::accent},
                                                {"wind", -4, 4, false, 0, &PixelFxSpawnSettings::wind}}};
    static const Descriptor            d{
        {PixelFxKind::Disintegrate, "spawn/disintegrate", 176, 100, 48, 86, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.spawn.wind   = 1;
            s.palette      = {hex(0x120a26), hex(0x2a1658), hex(0x3d2f9c), hex(0x3f68d4),
                              hex(0x3fa8ec), hex(0x86e0ff), hex(0xd6f8ff), WHITE};
            s.spawn.accent = {hex(0x7a3ae4), hex(0xb682ff), hex(0xecd2ff)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Disintegrate>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
