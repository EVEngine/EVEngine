// eve-hot-path
// Native adaptation of pixelfx nature/thorns.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureGeometry.h"
#include "stylize/pixelfx/NatureLeafGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RISE = .11, SINK = .36, D = 2.4;
struct Thorn {
    double   x, h, w, lean, hook, at, sink;
    uint32_t seed;
    bool     up = false, down = false;
};
void thorn(Paint& p, const Thorn& s, double h, std::span<const uint32_t> C, double base, bool white) {
    if (h < 1.5) return;
    const auto cx     = [&](double u) { return s.x + s.lean * u + s.hook * std::pow(u, 2.4); };
    const auto hw     = [&](double u) { return (s.w / 2) * std::pow(1 - u, .9) + .25; };
    const auto inside = [&](int px, int py) {
        const double u = (base - (py + .5)) / h;
        if (u < 0 || u > 1 || py + .5 > 0) return false;
        return std::abs(px + .5 - cx(u)) <= hw(u);
    };
    const int X0 = int(std::floor(s.x - s.w - 6)),
              X1 = int(std::ceil(s.x + s.w / 2 + std::abs(s.lean) + std::abs(s.hook) + 2));
    for (int py = int(std::floor(base - h)) - 1; py <= std::min(0., std::ceil(base)); ++py)
        for (int px = X0; px <= X1; ++px) {
            if (!inside(px, py)) {
                if (py <= 0 && (inside(px - 1, py) || inside(px + 1, py) || inside(px, py + 1) || inside(px, py - 1)))
                    p.dot(px, py, C[0]);
                continue;
            }
            if (white) {
                p.px(px, py, WHITE);
                continue;
            }
            const double u = (base - (py + .5)) / h, c = cx(u), r = hw(u), q = (px + .5 - (c - r)) / (2 * r);
            int          i = q < 0 ? 4 : q < .16 ? 6 : q < .36 ? 5 : q < .46 ? 7 : q < .7 ? 4 : q < .86 ? 3 : 2;
            if (u > .86) i = q < .6 ? 7 : 5;
            if (i < 7 && i > 2 && rand({jsUint(px), jsUint(std::floor((py + .5 - base) / 3)), s.seed}) < .12) --i;
            p.px(px, py, C[size_t(i)]);
        }
    if (!white && h > 14) {
        const double u0 = .34, u1 = .46, y0 = base - u0 * h, y1 = base - u1 * h, e0 = cx(u0) - hw(u0),
                     e1 = cx(u1) - hw(u1), L = s.w * .45;
        if (y0 <= 0) {
            p.tri(e0 + .5, y0, e1 + .5, y1, e1 - L, y1 - L * .7, C[3]);
            p.line(e1, y1, e1 - L, y1 - L * .7, C[6]);
            p.px(e1 - L, y1 - L * .7, C[7]);
            p.line(e0, y0 + 1, e1 - L + 1, y1 - L * .7 + 1, C[0]);
        }
    }
}
struct Chip {
    double             x, y, vx, vy, angle, angularVelocity, size;
    nature::StoneShape shape;
    double             life;
};
struct ChipEmission {
    double x, y, vx, vy, angle, angularVelocity, size;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct Thorns final : Kernel {
    const std::vector<uint32_t> C, G, S;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::vector<Thorn>          list;
    std::vector<Chip>           chips;
    double                      reach = 0, t = 0;
    explicit Thorns(const PixelFxSettings& o) : C(o.palette), G(o.nature.glow), S(o.nature.soil), R(o.seed) {
        ps.sparks   = {G[3], G[2], G[1], G[0]};
        const int n = std::max(2, o.nature.count);
        list.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            const double k = double(i) / (n - 1), h = 12 + 36 * k + R.range(-1.5, 1.5);
            list.push_back({10 + i * (104. / (n - 1)) + R.range(-3, 3), h, 6 + 9 * k, R.range(-1, 1.5) + k * 2,
                            h * R.range(.14, .22), .12 + i * .075, 1.45 + i * .06, uint32_t(R.integer(1, 999))});
        }
        reach = list.back().x + 8;
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void appendChip(const ChipEmission& e) {
        const auto   shape = nature::randomStoneShape(R);
        const double life  = std::min(R.range(.7, 1.1), D - .05 - t);
        chips.push_back({e.x, e.y, e.vx, e.vy, e.angle, e.angularVelocity, e.size, shape, life});
    }
    void chip(double x, double sp, int nn) {
        for (int i = 0; i < nn; ++i) {
            const double a = -PI / 2 + R.range(-1.1, 1.1), v = R.range(60, 210) * sp;
            appendChip({x + R.range(-3, 3), -1, std::cos(a) * v, std::sin(a) * v, R.range(0, TAU), R.range(-15, 15),
                        R.range(1.3, 2.6) * std::sqrt(sp)});
        }
    }
    double grown(const Thorn& s) const { return t < s.at ? 0 : ease::outBack(clamp((t - s.at) / RISE), 2.4); }
    double sunk(const Thorn& s) const { return smooth(s.sink, s.sink + SINK, t); }
    void   update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        for (auto& s : list) {
            const double k = s.h / 48;
            if (!s.up && t >= s.at) {
                s.up = true;
                lights.add(s.x, -s.h * .5, 18 + 18 * k, WHITE, .45, .08);
                lights.add(s.x, -s.h * .4, 26 + 26 * k, G[2], .45, .4);
                chip(s.x, .6 + .6 * k, 3 + int(jsRound(5 * k)));
                for (int i = 0; i < 10 + 18 * k; ++i) {
                    const double a = -PI / 2 + R.range(-1, 1), v = R.range(60, 230) * (.6 + .6 * k);
                    emit({ParticleKind::Grit, s.x + R.range(-s.w / 3, s.w / 3), -1, std::cos(a) * v, std::sin(a) * v,
                          R.range(.6, 1.1), R.pick<uint32_t>(std::array{S[5], S[4], S[3]}), R.isChance(.3) ? 2. : 1.});
                }
                for (int i = 0; i < 3 + 5 * k; ++i)
                    emit({ParticleKind::Smoke, s.x + R.range(-4, 4), -R.range(1, 4), R.signedRange(20, 80),
                          -R.range(5, 25), R.range(.7, 1.2), S[5], R.range(3, 5.5) * (.6 + .5 * k)});
                for (int i = 0; i < 2 + 3 * k; ++i)
                    emit({ParticleKind::Star, s.x + s.hook * .8 + R.range(-3, 3), -s.h * R.range(.5, 1),
                          R.range(-10, 10), -R.range(5, 20), R.range(.3, .6), G[3], R.range(.8, 2), R.range(0, .12)});
                for (int i = 0; i < 4; ++i)
                    emit({ParticleKind::Mote, s.x + R.range(-3, 3), -R.range(2, s.h * .6), R.range(-20, 20),
                          -R.range(30, 70), R.range(.4, .8), G[3], .7});
            }
            if (!s.down && t >= s.sink) {
                s.down = true;
                chip(s.x, .35, 2 + int(jsRound(2 * k)));
                for (int i = 0; i < 3 + 3 * k; ++i)
                    emit({ParticleKind::Dust, s.x + R.range(-s.w / 2, s.w / 2), -R.range(0, 3), R.signedRange(15, 50),
                          -R.range(2, 10), std::min(R.range(.4, .7), D - .05 - t), S[5], R.range(2, 4)});
            }
            if (s.up && !s.down && t > s.at + .2 && R.isChance(.05 * f))
                emit({ParticleKind::Star, s.x + s.lean * .5 - s.w * .15, -s.h * R.range(.3, .8), 0, -3,
                      R.range(.25, .45), C[7], R.range(.8, 1.8)});
        }
        for (size_t i = chips.size(); i > 0; --i) {
            auto&      c       = chips[i - 1];
            const bool expired = (c.life -= dt) <= 0;
            // The source steps its removed local chip too; no observers or RNG see it.
            c.vy += 950 * dt;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.angle += c.angularVelocity * dt;
            if (c.y > 0 && c.vy > 0) {
                c.y = 0;
                if (c.vy > 120) {
                    c.vy *= -.3;
                    c.vx *= .6;
                    c.angularVelocity *= .5;
                } else
                    c.vy = c.vx = c.angularVelocity = 0;
            }
            if (expired) chips.erase(chips.begin() + std::ptrdiff_t(i - 1));
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double run = ease::outCubic(clamp((t - .04) / .5)) * reach, fade = 1 - smooth(1.9, 2.3, t);
        for (int x = -2; x <= run; ++x) {
            const double hot = clamp(1 - (run - x) / 18) * (1 - smooth(.6, 1.2, t)), h = rand({jsUint(x), 31});
            p.dot(x, 0, hot > .4 ? G[3] : hot > .1 ? G[2] : S[0], fade);
            if (h < .5) p.dot(x, 1, S[1], fade * .8);
            if (h < .15) p.dot(x, -1, S[4], fade);
            if (hot > .3 && (x & 3) == 0) p.glow(x, 0, 6, G[2], .3 * hot);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        for (const auto& s : list) {
            const double g = grown(s), k = sunk(s);
            if (g <= 0 || k >= 1) continue;
            thorn(p, s, s.h * g, C, k * s.h * 1.05, t - s.at < .035);
            if (t - s.at < .3) p.glow(s.x, -s.h * .5 * g, s.h * .6 + 6, G[2], .35 * (1 - (t - s.at) / .3));
            const double lk = clamp((t - s.at - .04) / .18) * (1 - k), lw = s.w * .5;
            if (lk > .05)
                for (const auto row :
                     std::array<std::array<double, 3>, 3>{{{{-1, -2.5, 1}}, {{1, -.55, .9}}, {{-1, -1.9, .7}}}}) {
                    const double sd = row[0], a0 = row[1], sz = row[2],
                                 la = a0 + sd * .25 * (1 - lk) + std::sin(t * 4 + s.x) * .06,
                                 L  = (5 + s.w * .45) * sz * lk;
                    nature::drawLeaf(p, s.x + sd * lw + std::cos(la) * L * .5, -1 + std::sin(la) * L * .5, L,
                                     1.4 + s.w * .1 * sz, la, .35, G);
                }
            p.glow(s.x, -2, s.w + 6, G[2], .18 * lk);
        }
        for (const auto& c : chips)
            nature::drawRock(p, c.x, c.y, c.size, c.angle, c.shape, S, std::min(1., c.life / .25));
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& thornsDescriptor() {
    static const std::array<Option, 3> options{{{"glow", 1, 64, false, 4, &PixelFxNatureSettings::glow},
                                                {"soil", 1, 64, false, 8, &PixelFxNatureSettings::soil},
                                                {"count", 1, 64, false, 0, &PixelFxNatureSettings::count}}};
    static const Descriptor            d{
        {PixelFxKind::Thorns, "nature/thorns", 232, 92, 48, 78, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 8;
            s.nature.count = 6;
            s.palette      = {hex(0x160c0e), hex(0x2e1418), hex(0x4e2224), hex(0x743a30),
                              hex(0x9a5a3e), hex(0xc08458), hex(0xe2b484), hex(0xfbe8c8)};
            s.nature.glow  = {hex(0x123818), hex(0x2f7a2a), hex(0x6cb83a), hex(0xc8f070)};
            s.nature.soil  = {hex(0x120c0c), hex(0x251917), hex(0x3b2a24), hex(0x553d31),
                              hex(0x715440), hex(0x8f6e52), hex(0xb8966e), hex(0xe6d0a8)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Thorns>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
