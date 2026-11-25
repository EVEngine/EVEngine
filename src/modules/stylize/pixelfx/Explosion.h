// eve-hot-path
// Adapted from pixelfx explosions/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::explosion {
inline void lobe(Paint& p, double x, double y, double r, std::span<const uint32_t> FR, std::span<const uint32_t> SM,
                 double fire, double glow, double a, double z = 0, int dark = 0, int style = 0) {
    if (r < .7 || a <= .01) return;
    const int    n = int(FR.size()), m = int(SM.size()), back = z < -.35 ? 1 : 0;
    const double rim = 1 - std::min(.35, 1.6 / r), seam = 1 - std::min(.3, 1.3 / r),
                 soft = .55 + .4 * (1 - std::min(1., a));
    const auto   D    = [&](int i) { return SM[std::min(m - 1, i + back + dark)]; };
    p.shade(x - r, y - r, x + r, y + r, [&](double lx, double ly, int sx, int sy) {
        const double dx = (lx - x) / r, dy = (ly - y) / r, d2 = dx * dx + dy * dy;
        if (d2 > 1) return;
        const double d = std::sqrt(d2), nz = std::sqrt(1 - d2), sky = -.5 * dx - .66 * dy + .56 * nz,
                     v     = fire * (1.32 - .78 * d) + .14 * sky * fire;
        uint32_t     c     = 0;
        double       k     = 0;
        bool         solid = false;
        if (v > .44) {
            int i = std::min(n - 3, std::max(0, int(std::floor((1.3 - v) * (n - 1) * .78))));
            if (d > seam)
                i = std::min(6, std::max(4, i + 2));
            else if (d > seam - .22)
                i = std::min(n - 3, i + 1);
            c     = FR[i];
            k     = .05 * fire;
            solid = true;
        } else if (style) {
            const double lam = -.42 * dx - .78 * dy + .46 * nz;
            if (lam > .66 && d > .4)
                c = D(0);
            else if (lam > .16)
                c = D(1);
            else if (lam > -.3)
                c = D(2);
            else {
                c              = D(lam < -.64 ? 4 : 3);
                const double g = glow * (.6 + .4 * dy);
                if (g > .55) {
                    c = FR[g > .8 ? 5 : 6];
                    k = .08 * g;
                } else if (g > .34)
                    c = mix(c, FR[6], .6);
                else if (g > .16)
                    c = mix(c, FR[7], .45);
            }
        } else {
            const double g = glow * (.82 * dy + .3 * nz);
            if (g > .45) {
                c = FR[g > .66 ? 4 : 5];
                k = .1 * glow;
            } else if (g > .3)
                c = FR[6];
            else if (g > .2)
                c = FR[7];
            else {
                const double s = sky * .5 + .5;
                c              = D(s > .8 ? 0 : s > .62 ? 1 : s > .42 ? 2 : s > .22 ? 3 : 4);
            }
        }
        p.blendPx(sx, sy, c, solid || d <= rim ? a : a * soft);
        if (k > 0) p.add(sx, sy, c, k);
    });
}
struct PlumeOptions {
    double                x = 0, y = -20, rise = 100, tau = 1.2, drift = 4;
    std::array<double, 2> tube{8, 20}, ring{1, 18};
    double                grow = .9;
    int                   n    = 40;
    std::array<double, 2> size{5, 10};
    double swell = .5, spin = 2.4, spinTau = 1.6, heat = 1.15, cool = .45, under = .5, tilt = .2, squash = .7;
    std::array<double, 2>                fade{4, 6};
    double                               glow = 1, spread = 10;
    int                                  style = 0;
    bool                                 inner = false, hollow = false;
    std::optional<std::array<double, 3>> fray;
};
struct Plume {
    struct Lobe {
        double th, ph, j, s, h, dx, dy, f, gv;
    };
    struct Shape {
        double x, y, tube, ring, g;
    };
    struct Sample {
        double x, y, z, up, out, heat, fade, r, d, c, gv;
    };
    PlumeOptions      o;
    double            t = 0, roll = 0;
    std::vector<Lobe> lobes;
    explicit Plume(Rng& rng, PlumeOptions options = {}) : o(options) {
        lobes.reserve(o.n);
        for (int i = 0; i < o.n; ++i)
            lobes.push_back({TAU * std::fmod(i * .618034 + rng.range(-.08, .08), 1.),
                             TAU * (i + rng.range(-.35, .35)) / o.n, rng.range(-.55, .12),
                             rng.range(o.size[0], o.size[1]), rng.range(-.22, .08), rng.range(-1, 1), rng.range(-1, .4),
                             rng.range(-.9, .5), rng.range(.55, 1.15)});
    }
    Shape shape() const {
        const double u = std::max(0., t), g = 1 - std::exp(-u / o.grow);
        return {o.x, o.y - o.rise * (1 - std::exp(-u / o.tau)) - o.drift * u, lerp(o.tube[0], o.tube[1], g),
                lerp(o.ring[0], o.ring[1], g), g};
    }
    double hot() const { return clamp(o.heat - o.cool * t + .3); }
    double ember() const { return clamp((o.heat - o.cool * t + 1) / 1.1); }
    /** @brief Return an owning depth-sorted snapshot; caller-thread only, no callbacks.
     * @cost Linear storage and O(n log n) ordering per draw; bounded by authored lobe count.
     */
    std::vector<Sample> samples() const {
        const auto          S    = shape();
        const double        base = o.heat - o.cool * t, span = S.ring + S.tube == 0 ? 1 : S.ring + S.tube;
        std::vector<Sample> list;
        list.reserve(lobes.size());
        for (const auto& q : lobes) {
            const double fade  = 1 - smooth(o.fade[0] + q.f, o.fade[1] + q.f * .5, t),
                         loose = std::pow(smooth(o.fade[0] - 1.8 + q.f, o.fade[1], t), 1.5) * o.spread;
            if (fade <= 0) continue;
            const double th = q.th - roll, rt = S.tube * (1 + q.j), rho = S.ring + rt * std::cos(th), up = std::sin(th),
                         X = rho * std::cos(q.ph), Z = rho * std::sin(q.ph);
            const double heat = base + q.h - .18 * std::max(0., std::cos(th)) * clamp(t * 2) +
                                o.under * std::max(0., -up) * clamp(base + .6);
            const double r    = q.s * (.5 + .5 * S.g) * (1 + o.swell * clamp(1 - heat)) * (1 + .35 * (1 - fade));
            list.push_back({S.x + X + q.dx * loose, S.y - rt * up * o.squash + Z * o.tilt + q.dy * loose, Z / span, up,
                            std::cos(th), heat, fade, r, jsHypot2(X, Z) / span, 1 - std::min(1., std::abs(X) / span),
                            q.gv});
        }
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.z < b.z; });
        return list;
    }
    void update(double dt) {
        t += dt;
        roll += o.spin * (.12 + .88 * std::exp(-t / o.spinTau)) * dt;
    }
    /** @brief Advance and borrow RNG/fray callback synchronously on the caller thread; neither is retained.
     * @note The callback must not re-enter or mutate this plume.
     * @cost Late-fray depth sorting allocates proportional to the authored lobe count per active tick.
     */
    template <class Fray>
    void update(double dt, Rng& rng, Fray onFray) {
        update(dt);
        if (!o.fray || t <= (*o.fray)[0] || t >= (*o.fray)[2]) return;
        const auto   S = shape();
        const double k = (*o.fray)[1] * dt * smooth((*o.fray)[0], (*o.fray)[0] + 1, t);
        for (const auto& e : samples()) {
            if (e.out < .2 || !rng.isChance(k * e.fade)) continue;
            const double ux = e.x - S.x, uy = e.y - S.y, length = jsHypot2(ux, uy), l = length == 0 ? 1 : length;
            onFray(e.x + (ux / l) * e.r * .6, e.y + (uy / l) * e.r * .6, e.r, ux / l, uy / l);
        }
    }
    void draw(Paint& p, std::span<const uint32_t> FR, std::span<const uint32_t> SM) const {
        if (t >= o.fade[1] + .5) return;
        const auto   S    = shape();
        const double base = o.heat - o.cool * t, span = S.ring + S.tube == 0 ? 1 : S.ring + S.tube,
                     age = clamp(t / o.fade[1]), E = ember(), reach = .3 + 1.15 * E;
        for (const auto& e : samples()) {
            const double fire = clamp((e.heat - .2) / .75);
            const double glow =
                o.inner ? E * clamp((reach - e.d) / .45) * (.45 + .55 * std::max(0., -e.up)) * e.gv * o.glow
                        : clamp((base + .45) / .85) * o.glow * (.4 + .35 * std::max(0., -e.up) + .4 * e.c) * e.gv;
            lobe(p, e.x, e.y, e.r, FR, SM, fire, glow, e.fade * (.82 + .18 * fire), e.z,
                 (age > .72 ? 1 : 0) + (o.hollow && e.up < -.35 ? 1 : 0), o.style);
            if (fire > .25) p.glow(e.x, e.y, e.r * 1.25 + 2, FR[2], .07 * fire);
        }
        const double h = hot();
        if (h > 0) p.glow(S.x, S.y, span * 1.8 + 14, FR[3], .32 * h * o.glow);
        if (o.inner && E > .05) p.glow(S.x, S.y + S.tube * .3, span * (.5 + .7 * E) + 8, FR[4], .22 * E * E);
    }
};
struct ColumnOptions {
    double                w = 7, foot = 16, speed = 70;
    std::array<double, 2> size{5, 8};
    double                n = 18, sway = 2.5;
    int                   style = 0;
};
struct Column {
    struct Lobe {
        double y, x, s, ph, a, v;
    };
    ColumnOptions     o;
    double            t = 0, acc = 0;
    std::vector<Lobe> lobes;
    explicit Column(ColumnOptions options = {}) : o(options) {}
    void update(double dt, double top, double feed, Rng& rng) {
        const double L = std::max(10., -top);
        t += dt;
        if (feed > 0) {
            acc += dt * feed * o.n * (o.speed / L);
            for (; acc > 1; --acc)
                lobes.push_back(
                    {-1, rng.range(-1, 1), rng.range(o.size[0], o.size[1]), rng.range(0, TAU), 0, rng.range(.8, 1.2)});
        }
        for (size_t i = lobes.size(); i-- > 0;) {
            auto&        b = lobes[i];
            const double u = clamp(-b.y / L);
            b.y -= o.speed * b.v * (.45 + .55 * std::min(1., -b.y / 24)) * dt;
            b.a = std::min(1., b.a + dt * 4);
            if (b.y < top || (feed <= 0 && (b.a -= dt * (2 - u)) <= 0)) lobes.erase(lobes.begin() + i);
        }
    }
    void draw(Paint& p, double top, std::span<const uint32_t> FR, std::span<const uint32_t> SM, double fire,
              double glow) const {
        const double L    = std::max(10., -top);
        auto         list = lobes;
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
        for (const auto& b : list) {
            const double u = clamp(-b.y / L), foot = std::pow(1 - u, 4), w = o.w + o.foot * foot,
                         x = b.x * w * .45 + std::sin(t * 1.7 + b.ph + u * 3) * o.sway * u,
                         r = b.s * (.75 + .5 * foot + .35 * u) * std::min(1., b.a * 1.5), end = 1 - smooth(.85, 1, u);
            lobe(p, x, b.y - r * .3 * foot, r, FR, SM, clamp(fire * (1.1 - u * .9)), glow * smooth(.55, 1, u),
                 std::min(1., b.a) * end * .9, 0, u > .5 ? 0 : 1, o.style);
        }
    }
};
struct Stone {
    uint32_t light, body, shade, dust;
    explicit Stone(uint32_t c)
        : light(mix(scale(c, 1.35), WHITE, .08)),
          body(c),
          shade(scale(c, .64)),
          dust(mix(scale(c, 1.2), rgb(150, 140, 150), .3)) {}
};
inline std::array<uint32_t, 9> fireRamp(std::span<const uint32_t> F) {
    return {F[7], F[6], F[5], F[4], F[3], F[2], F[1], mix(F[1], F[0], .5), F[0]};
}
inline std::array<uint32_t, 10> heatRamp(std::span<const uint32_t> F) {
    return {
        F[0], F[1], F[2], F[3], F[4], F[5], F[6], F[7], mix(F[8], hex(0x0d0a12), .3), mix(F[8], hex(0x0d0a12), .65)};
}
inline void fireBall(Paint& p, const SimParticle& q, std::span<const uint32_t> FR) {
    const double k = q.life / q.max, age = 1 - k,
                 heat = std::min(.999, std::pow(age, .62) + std::fmod(q.seed, 1.) * .1);
    const int    n = int(FR.size()), idx = std::min(n - 2, int(std::floor(heat * (n - 1))));
    const double r = q.size * (.55 + .45 * std::min(1., age * 7)) * (1 - age * .55);
    const auto   c = FR[idx];
    if (idx >= n - 2) {
        p.disc(q.x, q.y, r, c, std::min(1., k * 3) * .85);
        return;
    }
    p.circle(q.x, q.y, r, FR[std::min(n - 1, idx + (idx <= 2 ? 3 : 2))]);
    p.circle(q.x - r * .16, q.y - r * .22, r * .78, c);
    if (r > 1.6) p.circle(q.x, q.y - r * .9, r * .42, c);
    if (idx <= 3 && r > 1.8) p.circle(q.x - r * .28, q.y - r * .34, r * .36, FR[std::max(0, idx - 1)]);
    if (idx < 6) p.glow(q.x, q.y, r * 2.4 + 2, c, .18 * k);
}
inline void drawPs(Paint& p, const Particles& ps, Paint::Layer layer, std::span<const uint32_t> FR) {
    const bool back  = layer == Paint::Layer::Back;
    const auto fires = [&] {
        for (const auto& q : ps.list)
            if (q.kind == ParticleKind::Fire && q.delay <= 0 && hasFlag(q.flags, ParticleFlag::Back) == back)
                fireBall(p, q, FR);
    };
    if (!back) fires();
    ps.draw(p, layer, ParticleKind::Fire);
    if (back) fires();
}
struct Draft {
    double x = 0, top = -INFINITY, w = 12, up = 150, squeeze = 4, pull = 0, low = 10, reach = 100, k = 1;
};
inline void draft(Particles& ps, double dt, Draft o) {
    if (o.k <= 0) return;
    for (auto& q : ps.list) {
        const bool lifts = q.kind == ParticleKind::Smoke || q.kind == ParticleKind::Dust ||
                           q.kind == ParticleKind::Fire || q.kind == ParticleKind::Ember ||
                           q.kind == ParticleKind::Vapour;
        if (q.delay > 0 || !lifts || q.y < o.top) continue;
        const double dx = q.x - o.x, ax = std::abs(dx), u = ax / o.w;
        if (u < 1) {
            const double f = (1 - u * u) * o.k;
            q.vy -= o.up * f * dt;
            q.vx -= dx * o.squeeze * f * dt;
        }
        if (o.pull && q.y > -o.low && ax > o.w * .4 && ax < o.reach)
            q.vx -= (dx < 0 ? -1 : dx > 0 ? 1 : 0) * o.pull * o.k * (1 - ax / o.reach) * dt;
    }
}
inline void cull(Particles& ps, double x0, double y0, double x1, double y1) {
    std::erase_if(ps.list, [&](const auto& q) { return q.x < x0 || q.x > x1 || q.y < y0 || q.y > y1; });
}
inline void scorch(Paint& p, std::span<const uint32_t> HR, double t, double rx0, double cool, double fade,
                   int rows = 3) {
    if (fade <= 0) return;
    const double rx = rx0 * std::min(1., t / .08), ry = rows + 1.5;
    const int    n = int(HR.size());
    for (int y = 0; y < rows; ++y)
        for (int x = int(std::floor(-rx)); x <= int(std::ceil(rx)); ++x) {
            const double dx = (x + .5) / rx, dy = (y + .5) / ry, d = std::sqrt(dx * dx + dy * dy);
            if (d > 1) continue;
            const double k     = rand({jsUint(x * 31 + y), 3}),
                         T     = std::pow(clamp(1 - t / cool), 1.4) * (1.25 - d) - k * .35,
                         crack = k > .88 && d < .85 ? std::pow(clamp(1 - t / (cool * 2)), 1.2) * (1.15 - d) * .8 : 0,
                         H     = std::max(T, crack);
            if (H > .06) {
                const auto c = HR[std::min(n - 1, int(std::floor((1 - std::min(1., H)) * (n - 1))))];
                p.dot(x, y, c, fade);
                if ((x & 3) == 0 || crack > T) p.glow(x + .5, y, 6, c, .18 * H * fade);
            } else
                p.dot(x, y, d < .5 ? hex(0x0d0a12) : hex(0x17121d), (d < .95 - k * .3 ? 1 : 1 - d) * fade);
        }
}
inline void rock(Paint& p, const Debris& b, const Stone& st, std::span<const uint32_t> FR, double heat = 0) {
    const double r = .6 + b.size * .75, fade = std::min(1., b.life / .5), lx = std::cos(b.angle) * r * .4,
                 ly = std::sin(b.angle) * r * .4;
    p.shader(b.x - r - 1, b.y - r - 1, b.x + r + 1, b.y + r + 1, [&](double x, double y, ShadeOutput& out) {
        const double dx = x - b.x, dy = y - b.y, d = jsHypot2(dx * 1.15, dy);
        if (d > r + .9) return;
        out.a = fade;
        if (d > r) {
            out.c = hex(0x1a1422);
            return;
        }
        const double lit = dx * lx + dy * ly;
        if (heat > .08) {
            const double g = heat * (1.1 - d / (r + .5)) + (lit > .3 ? heat * .45 : 0);
            if (g > .5) {
                out.c = FR[g > 1 ? 1 : g > .8 ? 2 : g > .62 ? 3 : 4];
                return;
            }
            if (g > .32) {
                out.c = FR[5];
                return;
            }
        }
        out.c = lit < -.4 ? st.light : lit > .6 ? st.shade : st.body;
    });
    if (heat > .05) p.glow(b.x, b.y, r * 2 + 3, FR[3], .32 * heat * fade);
}
inline void dome(Paint& p, std::span<const uint32_t> F, double x, double r, double k) {
    const double e = 1.4 / r;
    p.shader(x - r - 1.5, -r - 1.5, x + r + 1.5, .5, [&](double px, double py, ShadeOutput& out) {
        const double d = jsHypot2(px - x, py) / r;
        if (d > 1 + e) return;
        out.c = d > 1    ? F[2]
                : d > .8 ? (k > .45 ? F[6] : F[5])
                : d > .5 ? (k > .45 ? WHITE : F[6])
                : k > .2 ? WHITE
                         : F[6];
    });
}
inline void glowDome(Paint& p, double x, double y, double r, double w, uint32_t c, double k,
                     std::array<double, 4> box = {-INFINITY, -INFINITY, INFINITY, INFINITY}) {
    if (k <= .004 || w < 1) return;
    p.shader(std::max(box[0], x - r - w), std::max(box[1], y - r - w), std::min(box[2], x + r + w),
             std::min(box[3], y + .5), [&](double px, double py, ShadeOutput& out) {
                 const double d = std::abs(jsHypot2(px - x, py - y) - r) / w;
                 if (d >= 1) return;
                 out.l = c;
                 out.k = (1 - d) * (1 - d) * k;
             });
}
/** @brief Blend an ellipse; color/alpha callables are borrowed only for this synchronous raster pass.
 * @note Callables must not mutate or re-enter the painter; caller-thread only.
 */
template <class C, class A>
void haze(Paint& p, double x, double y, double rx, double ry, C c, A alpha) {
    if (rx < .5 || ry < .5) return;
    p.shade(x - rx, y - ry, x + rx, y + ry, [&](double lx, double ly, int sx, int sy) {
        const double dx = (lx - x) / rx, dy = (ly - y) / ry, d2 = dx * dx + dy * dy;
        if (d2 > 1) return;
        const double d = std::sqrt(d2);
        p.blendPx(sx, sy, sample(c, d), sample(alpha, d));
    });
}
inline void pool(Paint& p, double x, double y, double rx, double ry, uint32_t c, double k) {
    if (k <= .004 || rx < 1 || ry < .5) return;
    p.shader(x - rx, y - ry, x + rx, y + ry, [&](double px, double py, ShadeOutput& out) {
        const double dx = (px - x) / rx, dy = (py - y) / ry, d = dx * dx + dy * dy;
        if (d >= 1) return;
        const double f = 1 - std::sqrt(d);
        out.l          = c;
        out.k          = f * f * k;
    });
}
struct TrailPoint {
    double x, y, t;
};
inline void trail(Paint& p, std::span<const TrailPoint> pts, double t, std::span<const uint32_t> FR,
                  std::span<const uint32_t> SM, double life = .5, double w = 1, double heat = 1) {
    double lx = NAN, ly = NAN;
    for (size_t i = pts.size(); i-- > 0;) {
        const auto&  q   = pts[i];
        const double age = (t - q.t) / life;
        if (age >= 1) break;
        if (jsHypot2(q.x - lx, q.y - ly) < 1.6) continue;
        lx             = q.x;
        ly             = q.y;
        const double r = w * (.7 + age * 2.6);
        if (age < .12 * heat)
            p.circle(q.x, q.y - age * 6, r * .8, FR[age < .05 ? 3 : 5]);
        else
            p.disc(q.x, q.y - age * 6, r, SM[std::min(SM.size() - 1, size_t(1 + std::floor(age * 3)))],
                   .55 * (1 - age));
    }
}
inline void fadeEdges(Particles& ps, double x0, double x1, double w = 30, double y1 = INFINITY) {
    for (size_t i = ps.list.size(); i-- > 0;) {
        auto& q = ps.list[i];
        if (!isSoft(q.kind)) continue;
        const double r = q.size * 1.65, e = std::min((std::min(q.x - x0, x1 - q.x) - r) / w, (y1 - q.y - r) / 6);
        if (e <= 0)
            ps.list.erase(ps.list.begin() + std::ptrdiff_t(i));
        else if (e < 1)
            q.life = std::min(q.life, q.max * .42 * e);
    }
}
inline void drawDirt(Paint& p, const Particles& ps, Paint::Layer layer) {
    ps.draw(p, layer, ParticleKind::Grit);
    if (layer == Paint::Layer::Back) return;
    for (const auto& q : ps.list) {
        if (q.kind != ParticleKind::Grit || q.delay > 0) continue;
        const double k = q.life / q.max, sp = jsHypot2(q.vx, q.vy);
        if (k < .25 && k * 4 < .5) continue;
        if (sp > 140) {
            const double L = std::min(5., sp * .012), ux = q.vx / sp, uy = q.vy / sp;
            p.lineFn(
                q.x - ux * L, q.y - uy * L, q.x, q.y, [&](double u) { return u < .5 ? scale(q.color, .7) : q.color; },
                1.);
        } else {
            p.px(q.x, q.y, q.color);
            if (q.size >= 2) {
                p.px(q.x + 1, q.y, q.color);
                p.px(q.x, q.y + 1, scale(q.color, .8));
                p.px(q.x + 1, q.y + 1, scale(q.color, .7));
            }
        }
    }
}
inline void crater(Paint& p, std::span<const uint32_t> E, double x, double r, double k, double a = 1) {
    if (k <= 0 || a <= 0) return;
    const size_t n  = E.size();
    const double rx = r * k, ry = std::max(1.5, r * .17 * k);
    for (int py = -1; py <= int(std::ceil(ry)) + 1; ++py)
        for (int px = int(std::floor(-rx - 1)); px <= int(std::ceil(rx + 1)); ++px) {
            const double dx = (px + .5) / rx, dy = (py + .5 - ry * .35) / ry, d = std::sqrt(dx * dx + dy * dy);
            if (d > 1.08) continue;
            const auto c = d > .92 ? (dy < 0 ? E[n - 2] : E[2]) : dy < -.2 ? E[0] : dy < .35 ? E[1] : E[2];
            p.dot(x + px, py, c, a);
        }
    for (int side : {-1, 1})
        for (int i = 0; i < 9; ++i) {
            const double u  = i / 8.,
                         h  = jsRound(std::sin(PI * u) * 2.2 * k +
                                      (rand({uint32_t(i), side > 0 ? 3u : 4u}) < .4 ? 1 : 0) * k),
                         hx = x + side * (rx * .82 + u * r * .55);
            for (int j = 0; j <= h; ++j) p.dot(hx, -j, j == h ? E[n - 2] : j == h - 1 ? E[3] : E[2], a);
        }
    for (uint32_t i = 0; i < 26; ++i) {
        const int    side = rand({i, 7}) < .5 ? -1 : 1;
        const double d = rx * (1.05 + rand({i, 8}) * 1.4), y = jsRound(rand({i, 9}) * 3) - 1;
        if (rand({i, 10}) < k) p.dot(x + side * d, y, rand({i, 11}) < .5 ? E[3] : E[2], a);
    }
}
}  // namespace eve::stylize::pixelfx::explosion
