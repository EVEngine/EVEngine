// eve-hot-path
// Native adaptation of pixelfx nature/healing-bloom.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureLeafGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double STEM = .42, BUD = .75, OPEN = 1.05, LIFT = 1.7, WILT = 2.4, H = 36, D = 3.3;
struct Petal {
    double az, offset;
    bool   gone;
    double at;
};
struct Flying {
    double x, y, vx, vy, angle, angularVelocity, flip, flipVelocity, life, max;
};
struct Glyph {
    double x, y, t0, life;
    int    length;
    double seed;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
void cross(Paint& p, double x, double y, int length, std::span<const uint32_t> C, double alpha, bool glint) {
    const double X = jsRound(x), Y = jsRound(y);
    const auto   inside = [length](int dx, int dy) {
        return (dx == 0 && std::abs(dy) <= length) || (dy == 0 && std::abs(dx) <= length);
    };
    for (int dy = -length - 1; dy <= length + 1; ++dy)
        for (int dx = -length - 1; dx <= length + 1; ++dx) {
            if (inside(dx, dy))
                p.dot(X + dx, Y + dy, glint && !dx && !dy ? WHITE : dy < 0 || dx < 0 ? C[6] : C[5], alpha);
            else if (length > 1 &&
                     (inside(dx - 1, dy) || inside(dx + 1, dy) || inside(dx, dy - 1) || inside(dx, dy + 1)))
                p.dot(X + dx, Y + dy, C[2], alpha * .8);
        }
    p.glow(x, y, 5 + length * 2, C[6], .35 * alpha);
}
struct HealingBloom final : Kernel {
    const std::vector<uint32_t> C, Pt, Ht;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::vector<Flying>         flying;
    std::vector<Glyph>          glyphs;
    std::array<Petal, 6>        petals;
    double                      t      = 0;
    bool                        popped = false, sprouted = false;
    explicit HealingBloom(const PixelFxSettings& o) : C(o.palette), Pt(o.nature.petal), Ht(o.nature.heart), R(o.seed) {
        ps.sparks = {Ht[3], Ht[2], Ht[1], Ht[0]};
        for (size_t i = 0; i < petals.size(); ++i)
            petals[i] = {(double(i) / 6) * TAU + .3, std::fmod(double(i) * .7, 1), false,
                         LIFT + std::fmod(double(i) * .37, 1) * .55};
    }
    double grow() const { return ease::outCubic(clamp(t / STEM)) * (1 - ease::inCubic(clamp((t - WILT) / .6))); }
    double sway() const { return std::sin(t * 2.6) * 1.4 * smooth(.2, .6, t); }
    std::array<Point, 13> stem() const {
        std::array<Point, 13> pts;
        const double          g = grow();
        for (size_t i = 0; i < pts.size(); ++i) {
            const double s = double(i) / 12;
            pts[i]         = {std::sin(s * 3.2) * 2.2 * s + sway() * s * s, (1 - g) * H * 1.1 - s * H};
        }
        return pts;
    }
    double open() const { return ease::outBack(clamp((t - BUD) / (OPEN - BUD)), 1.4); }
    void   emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void   update(double dt) override {
        t += dt;
        const double f   = std::min(1., dt * 120);
        const auto   top = stem()[12];
        if (!sprouted) {
            sprouted = true;
            for (int i = 0; i < 8; ++i)
                emit({ParticleKind::Grit, R.range(-3, 3), -1, R.range(-40, 40), -R.range(40, 100), R.range(.5, .8),
                      R.pick<uint32_t>(std::array<uint32_t, 2>{{C[2], C[3]}}), 1});
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Dust, R.range(-3, 3), -1, R.signedRange(10, 30), -R.range(4, 10), R.range(.4, .6),
                      C[3], R.range(1.5, 2.5)});
        }
        if (t > STEM && t < BUD && R.isChance(.3 * f)) {
            const double a = R.range(0, TAU), d = R.range(14, 26), x = top.x + std::cos(a) * d,
                         y = top.y - 3 + std::sin(a) * d * .8;
            emit({ParticleKind::Streak, x, y, (top.x - x) * 3.4, (top.y - 3 - y) * 3.4, R.range(.18, .26),
                  R.isChance(.5) ? Ht[2] : C[6]});
        }
        if (!popped && t >= OPEN - .12) {
            popped = true;
            lights.add(top.x, top.y - 3, 22, WHITE, .45, .08);
            lights.add(top.x, top.y - 3, 70, Ht[2], .5, .8);
            for (int i = 0; i < 16; ++i) {
                const double a = R.range(0, TAU), sp = R.range(40, 120);
                emit({ParticleKind::Mote, top.x, top.y - 3, std::cos(a) * sp, std::sin(a) * sp - 20, R.range(.4, .8),
                      R.isChance(.5) ? Ht[3] : C[7], .7});
            }
            for (int i = 0; i < 5; ++i)
                emit({ParticleKind::Star, top.x + R.range(-10, 10), top.y - R.range(0, 14), 0, -6, R.range(.3, .6),
                      Ht[3], R.range(1, 2.4), R.range(0, .3)});
        }
        if (t > OPEN && t < WILT && R.isChance(.13 * f))
            glyphs.push_back({R.range(-22, 22), -R.range(2, 36), t, std::min(R.range(.7, 1.1), D - .05 - t),
                              R.isChance(.35) ? 3 : 2, R.next() * 9});
        if (t > OPEN - .1 && t < WILT && R.isChance(.35 * f))
            emit({ParticleKind::Ember, R.range(-20, 20), -R.range(0, 8), R.range(-6, 6), -R.range(25, 55),
                  std::min(R.range(.6, 1.1), D - .05 - t), R.isChance(.6) ? C[6] : Ht[2], R.isChance(.3) ? 2. : 1.});
        for (size_t i = glyphs.size(); i > 0; --i)
            if (t - glyphs[i - 1].t0 > glyphs[i - 1].life) glyphs.erase(glyphs.begin() + std::ptrdiff_t(i - 1));
        for (auto& q : petals)
            if (!q.gone && t >= q.at) {
                q.gone         = true;
                const double e = PI / 2 - .15, dx = std::sin(e) * std::cos(q.az), length = 10;
                flying.push_back({top.x + dx * length * .6, top.y - 3 - std::cos(e) * length * .6,
                                  dx * 30 + R.range(-10, 10), -R.range(25, 45), std::atan2(-std::cos(e), dx),
                                  R.range(-5, 5), 0, R.range(5, 9), std::min(R.range(.75, 1.), D - .45 - t), 0});
                flying.back().max = flying.back().life;
            }
        for (size_t i = flying.size(); i > 0; --i) {
            auto& q = flying[i - 1];
            q.life -= dt;
            if (q.life <= 0) {
                emit({ParticleKind::Star, q.x, q.y, 0, -8, std::min(.35, D - .05 - t), Pt[3], 1.8});
                flying.erase(flying.begin() + std::ptrdiff_t(i - 1));
                continue;
            }
            q.vx -= q.vx * 1.5 * dt;
            q.vy -= q.vy * .8 * dt + 10 * dt;
            q.x += q.vx * dt + std::sin(q.flip) * 12 * dt;
            q.y += q.vy * dt;
            q.angle += q.angularVelocity * dt;
            q.flip += q.flipVelocity * dt;
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void petal(Paint& p, const Petal& q, Point top) const {
        const double op = open(), e = lerp(.15, PI / 2 - .2, op), dx = std::sin(e) * std::cos(q.az),
                     dz = std::sin(e) * std::sin(q.az), dy = -std::cos(e), length = lerp(8, 12, op),
                     len = length * jsHypot2(dx, dy), angle = std::atan2(dy, dx), cy = top.y - 3;
        const bool   back = dz < 0;
        if (len < 1.5) return;
        const std::array<uint32_t, 4> ramp = back ? std::array<uint32_t, 4>{{Pt[0], Pt[0], Pt[1], Pt[2]}}
                                                  : std::array<uint32_t, 4>{{Pt[0], Pt[1], Pt[2], Pt[3]}};
        nature::drawLeaf(p, top.x + dx * length * .5, cy + dy * length * .5, len + 1, 3 + (1 - std::abs(dz)) * .8,
                         angle, back ? .9 : .2, ramp);
    }
    void drawBack(Paint& p) const override {
        const double bloom = smooth(OPEN - .15, OPEN + .2, t) * (1 - smooth(WILT - .2, WILT + .5, t));
        const auto   top   = stem()[12];
        for (int h = 0; h < 70; h += 7) p.glow(0, -h, 22, C[5], .12 * bloom * (1 - double(h) / 80));
        const double k = clamp((t - OPEN + .12) / .6);
        if (k > 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{{C[3], C[5], Ht[2], Ht[3]}};
            band(p, 0, .5, 6 + 30 * ease::outCubic(k), .18, std::max(1., 3 * (1 - k)), 1.1 - k, ramp,
                 [](double g) { return std::sin(g) < 0; });
        }
        const double fw = 6 + 16 * smooth(0, .8, t);
        for (int x = int(std::floor(-fw)); x <= fw; ++x) {
            const double u = std::abs(x) / fw,
                         a = std::sqrt(1 - u) * smooth(0, .4, t) * (1 - smooth(WILT, WILT + .6, t));
            p.dot(x, 0, u < .3 ? C[6] : u < .65 ? C[5] : C[4], a);
            if (x % 5 == 0) p.glow(x, 0, 8, C[5], .2 * a * (1 - u));
        }
        if (t > BUD - .05)
            for (const auto& q : petals)
                if (std::sin(q.az) < 0 && !q.gone) petal(p, q, top);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const auto   pts = stem();
        const auto   top = pts[12];
        const double g   = grow();
        if (g > .02) {
            for (size_t i = 1; i < pts.size(); ++i) {
                const auto a = pts[i - 1], b = pts[i];
                if (b.y > 0) continue;
                p.line(a.x, a.y, b.x, b.y, C[5]);
                p.line(a.x + 1, a.y, b.x + 1, b.y, C[3]);
            }
            for (const auto row : std::array<std::array<double, 3>, 2>{{{{4, -1, .1}}, {{7, 1, .22}}}}) {
                const double sd = row[1], d = row[2],
                             lk = ease::outBack(clamp((t - d - .12) / .3), 1.6) * (1 - smooth(WILT, WILT + .5, t) * .4);
                const auto   q  = pts[size_t(row[0])];
                if (lk <= 0 || q.y > -1) continue;
                const double                  la = sd < 0 ? -2.6 + .4 * (1 - lk) : -.55 - .4 * (1 - lk);
                const std::array<uint32_t, 4> ramp{{C[1], C[3], C[5], C[6]}};
                nature::drawLeaf(p, q.x + std::cos(la) * 6 * lk, q.y + std::sin(la) * 6 * lk, 12 * lk, 3.4, la, .25,
                                 ramp);
            }
            if (t > STEM - .1 && t < OPEN - .1) {
                const double b = clamp((t - STEM + .1) / (BUD - STEM)), w = 2 + 2.5 * b, h = 3.5 + 5 * b;
                p.ellipse(top.x, top.y - h * .6, w, h, [&](double d, double a) {
                    return std::cos(a) < -.2 ? Pt[2] : d > .7 ? Pt[0] : Pt[1];
                });
                p.glow(top.x, top.y - h * .6, 8 + 8 * b, Ht[2], .25 * b);
            }
        }
        for (const auto& q : petals)
            if (std::sin(q.az) >= 0 && !q.gone && t > BUD - .05) petal(p, q, top);
        if (t > OPEN - .12 && t < WILT + .3) {
            const double pulse = .85 + .15 * std::sin(t * 9), k = 1 - smooth(WILT - .3, WILT + .3, t), cy = top.y - 4;
            p.circle(top.x, cy, 3.3 * k, Ht[1]);
            p.circle(top.x - .5, cy - .5, 2.2 * k, Ht[2]);
            p.px(top.x - 1, cy - 1, Ht[3]);
            p.glow(top.x, cy, 16 * pulse, Ht[2], .5 * k);
            p.glow(top.x, cy, 34, C[5], .2 * k);
        }
        const double e = t - OPEN + .12;
        if (e > 0 && e < .16) p.sparkle(top.x, top.y - 3, 10 * (1 - e / .16), Ht[3], Ht[2], .6);
        for (const auto& q : flying)
            nature::drawLeaf(p, q.x, q.y, 6, 2.2, q.angle, q.flip, Pt, std::min(1., q.life / .3));
        for (const auto& q : glyphs) {
            const double k = (t - q.t0) / q.life;
            cross(p, q.x + std::sin(k * 5 + q.seed) * 2, q.y - k * 24, q.length, C, std::sin(PI * k),
                  std::sin(k * 14 + q.seed) > .8);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& healingBloomDescriptor() {
    static const std::array<Option, 2> options{{{"petal", 1, 64, false, 4, &PixelFxNatureSettings::petal},
                                                {"heart", 1, 64, false, 4, &PixelFxNatureSettings::heart}}};
    static const Descriptor            d{
        {PixelFxKind::HealingBloom, "nature/healing-bloom", 124, 120, 62, 108, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 4;
            s.nature.petal = {hex(0x4a1a3a), hex(0xa8406e), hex(0xec8ab0), hex(0xffd8e8)};
            s.nature.heart = {hex(0xa8641a), hex(0xf0b42e), hex(0xffe680), hex(0xfffbe0)};
            s.palette      = {hex(0x0c1a10), hex(0x173a1a), hex(0x24582a), hex(0x357a34),
                              hex(0x52a03e), hex(0x86c84e), hex(0xc4ec7a), hex(0xf4ffd0)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HealingBloom>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
