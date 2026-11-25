// eve-hot-path
// Native adaptation of pixelfx holy/heal.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          D = 1.9, SY = .3, PR = 16, LIFT0 = .16, LIFT1 = .74, TOPY = -43;
constexpr std::array<std::string_view, 4> HEART{".#.#.", "#####", ".###.", "..#.."};
void                                      heart(Paint& p, double x, double y, std::span<const uint32_t> C, double a) {
    const double X = jsRound(x) - 2, Y = jsRound(y) - 2;
    const auto   at = [](int i, int j) {
        return j >= 0 && j < 4 && i >= 0 && i < 5 && HEART[size_t(j)][size_t(i)] == '#';
    };
    for (int j = -1; j <= 4; ++j)
        for (int i = -1; i <= 5; ++i) {
            if (at(i, j))
                p.dot(X + i, Y + j, i == 1 && j == 1 ? WHITE : j >= 2 ? C[5] : C[6], a);
            else if (at(i - 1, j) || at(i + 1, j) || at(i, j - 1) || at(i, j + 1))
                p.dot(X + i, Y + j, C[3], a * .9);
        }
    p.glow(x, y, 6, C[5], .4 * a);
}
struct Petal {
    double angle, start, width;
};
struct Mote {
    double start, angle, radius, y, omega, velocity, life;
    int    kind;
};
struct EdgeRow {
    double              y;
    std::vector<double> xs;
};
struct Heal final : Kernel {
    const std::vector<uint32_t> C;
    const PixelFxActor          A;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::vector<EdgeRow>        rows;
    std::array<Petal, 8>        petals;
    std::array<Mote, 16>        motes;
    double                      t = 0, lastRow = 1;
    bool                        burst = false;
    explicit Heal(const PixelFxSettings& o) : C(o.palette), A(o.actor), rng(o.seed) {
        ps.sparks        = {WHITE, C[6], C[5], C[4]};
        const auto shape = holy::prepareShape(A);
        for (auto point : shape.edge) {
            if (rows.empty() || rows.back().y != point.y) rows.push_back({point.y, {}});
            rows.back().xs.push_back(point.x);
        }
        for (int k = 0; k < 8; ++k)
            petals[size_t(k)] = {(double(k) / 8) * TAU + .2, .02 + ((k * 3) % 8) * .035, rng.range(4.2, 5.2)};
        for (int k = 0; k < 8; ++k) {
            const double velocity = rng.range(46, 58), life = rng.range(.75, .95);
            motes[size_t(k)] = {
                .2 + k * .05, petals[size_t(k)].angle, PR * .9, -1, 4.2 * (k % 2 ? 1 : -1), velocity, life, k % 2};
        }
        for (int k = 0; k < 8; ++k) {
            const double start = LIFT1 + rng.range(0, .06), radius = rng.range(12, 18), y = TOPY + rng.range(-2, 4),
                         velocity = rng.range(14, 34), life = rng.range(.35, .6);
            motes[size_t(8 + k)] = {
                start, (double(k) / 8) * TAU, radius, y, 3.6 * (k % 2 ? 1 : -1), velocity, life, (k + 1) % 2};
        }
    }
    double hoopY(double tt) const { return lerp(0, TOPY, ease::inOutSine(seg(tt, LIFT0, LIFT1))); }
    double hoopR(double tt) const { return lerp(PR, 12, seg(tt, LIFT0, LIFT1)); }
    double bloom(size_t k, double tt) const {
        const auto pe = petals[k];
        return ease::outBack(seg(tt, pe.start, pe.start + .3), 1.8) *
               (1 - ease::inBack(seg(tt, 1.25 + double(k) * .02, 1.6 + double(k) * .02), 1.4));
    }
    void petal(Paint& p, size_t k, bool back) const {
        const auto   pe = petals[k];
        const double g = pe.angle + t * .4, b = bloom(k, t);
        const bool   near = std::sin(g) >= 0;
        if (near == back || b <= .02) return;
        const double ca = std::cos(g), sa = std::sin(g), rb = 6, rt = rb + 9 * std::min(1.2, b),
                     h = (k % 2 ? 7 : 9) * b, bx = ca * rb, by = sa * rb * SY, tx = ca * rt, ty = sa * rt * SY - h,
                     L = jsHypot2(tx - bx, ty - by);
        if (L < 1.5) return;
        const double ux = (tx - bx) / L, uy = (ty - by) / L,
                     W = pe.width * std::min(1., b) * (.35 + .65 * std::abs(sa)), pad = W + 1;
        p.shade(std::min(bx, tx) - pad, std::min(by, ty) - pad, std::max(bx, tx) + pad, std::max(by, ty) + pad,
                [&](double x, double y, int sx, int sy) {
                    const double dx = x - bx, dy = y - by, u = (dx * ux + dy * uy) / L;
                    if (u < 0 || u > 1) return;
                    const double hw = W * std::pow(std::sin(PI * std::pow(u, .6)), .7) + .2,
                                 v  = std::abs(-dx * uy + dy * ux) / hw;
                    if (v > 1) return;
                    if (u > .9) {
                        p.put(sx, sy, WHITE, 1);
                        return;
                    }
                    if (v > .7) {
                        p.put(sx, sy, u < .3 ? C[3] : u < .7 ? C[5] : C[6], 1);
                        return;
                    }
                    if (v < .2 && u > .15 && u < .85) {
                        p.put(sx, sy, u < .55 ? C[5] : C[6], back ? .75 : .85);
                        return;
                    }
                    p.blendPx(sx, sy, u < .45 ? C[3] : C[4], back ? .6 : .45);
                });
        p.glow(tx, ty, 5, C[5], .35 * std::min(1., b));
    }
    void hoop(Paint& p, bool back) const {
        if (t < LIFT0 || t > LIFT1 + .12) return;
        const double y = hoopY(t), r = hoopR(t) * (t > LIFT1 ? 1 - (t - LIFT1) / .12 : 1);
        const auto   keep = [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; };
        holy::oval(p, 0, y, r, r * SY, [&](double g) { return std::sin(g) > .5 ? WHITE : C[6]; }, back ? .8 : 1, keep);
        if (!back) {
            holy::oval(p, 0, y + 1, r - .5, r * SY, C[4], 1., keep);
            for (int i = -2; i <= 2; ++i) p.glow(i * r * .45, y, 8, C[5], .32);
        }
    }
    void riders(Paint& p, bool back) const {
        for (auto m : motes) {
            const double age = t - m.start;
            if (age < 0) continue;
            const auto at = [&](double aa) {
                const double th = m.angle + m.omega * aa, r = m.radius * (1 - .45 * (aa / m.life));
                return std::array<double, 3>{std::cos(th) * r, m.y - m.velocity * aa, std::sin(th)};
            };
            if (age < m.life) {
                const auto [x, y, z] = at(age);
                if ((z < 0) != back) continue;
                for (int j = 0; j < 7; ++j) {
                    const double aj = age - j * .03;
                    if (aj < 0) break;
                    const auto [tx, ty, tz] = at(aj);
                    if ((tz < 0) != back) continue;
                    p.dot(tx, ty, j == 0 ? WHITE : j < 3 ? C[6] : C[5], (1 - double(j) / 7) * (back ? .6 : 1));
                }
                p.glow(x, y, 4, C[5], back ? .2 : .4);
                continue;
            }
            const double u = (age - m.life) / .36;
            if (u >= 1 || back) continue;
            const auto   pos = at(m.life);
            const double x = pos[0], y = pos[1], k = 1 - smooth(.5, 1, u);
            if (u < .12)
                p.sparkle(x, y, 3, WHITE, C[6], .5);
            else if (m.kind)
                heart(p, x, y - u * 6, C, k);
            else
                p.sparkle(x, y - u * 5, 1 + 2 * k * (.7 + .3 * std::sin(age * 30)), WHITE, C[5], .45 * k);
        }
    }
    void update(double dt) override {
        t += dt;
        if (t > LIFT0 && t < LIFT1) {
            const double Y = jsRound(hoopY(t));
            for (double y = lastRow; y > Y; --y) {
                const EdgeRow* row = nullptr;
                for (const auto& candidate : rows)
                    if (candidate.y == y) {
                        row = &candidate;
                        break;
                    }
                if (!row || !rng.isChance(.45)) continue;
                const double picked = row->xs[size_t(std::floor(rng.next() * double(row->xs.size())))],
                             x = picked + rng.range(-1, 1) * 2, vx = rng.range(-4, 4), vy = -rng.range(4, 12),
                             life  = rng.range(.3, .55);
                const auto   color = rng.isChance(.5) ? WHITE : C[6];
                const double size  = rng.range(.6, 1.6);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, color, size);
            }
            lastRow = std::min(lastRow, Y);
        }
        if (!burst && t >= LIFT1) {
            burst = true;
            lights.add(0, TOPY, 26, C[5], .55, .3);
            lights.add(0, TOPY, 10, WHITE, .45, .08);
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double pool = smooth(0, .2, t) * (1 - smooth(1.3, 1.75, t));
        if (pool > 0) p.glow(0, -2, 30, C[4], .3 * pool);
        for (size_t k = 0; k < 8; ++k) petal(p, k, true);
        hoop(p, true);
        ps.draw(p, Paint::Layer::Back);
        riders(p, true);
    }
    void draw(Paint& p) const override {
        const double drain = smooth(0, .08, t);
        if (t < LIFT1 + .35) {
            const double Y    = t < LIFT0 ? 1 : hoopY(t);
            const bool   done = t >= LIFT1;
            holy::recolor(
                p, A,
                [&](uint32_t c, double, double y) -> uint32_t {
                    const double d = y - Y;
                    if (!done && d < -.5) {
                        const double l = luma(c) * .88;
                        return mix(c, rgb(l * 236, l * 226, l * 222), .68 * drain);
                    }
                    if (!done && d < 1) return WHITE;
                    if (!done && d < 2.5) return C[6];
                    const double k = done ? .45 * (1 - seg(t, LIFT1, LIFT1 + .35)) : .5 * std::exp(-(d - 2.5) / 5);
                    return k > .05 ? mix(c, C[5], k) : 0;
                },
                1.);
            if (t > LIFT0 && !done) p.glow(0, Y, 18, C[5], .35);
        }
        for (size_t k = 0; k < 8; ++k) petal(p, k, false);
        hoop(p, false);
        if (t >= LIFT1 && t < LIFT1 + .25) p.sparkle(0, TOPY, 8 * (1 - (t - LIFT1) / .25), WHITE, C[5], .6);
        riders(p, false);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& healDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Heal, "holy/heal", 80, 104, 40, 92, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 2;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x3d2027), hex(0x7d3f35), hex(0xc97a4a), hex(0xf2b46a),
                              hex(0xffd994), hex(0xfff0cf), hex(0xfffaf2), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Heal>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
