// Adapted from pixelfx fire/_lib.js. Copyright (c) 2026 Hugo Duprez and Patch.
// MIT; see LICENSE.txt. All RNG streams are explicit; no external references retained.
#pragma once
#include "stylize/pixelfx/Simulation.h"

namespace eve::stylize::pixelfx::fire {
struct Noise {
    double duration, time;
    double at(double t, double rate, uint32_t seed = 0, double offset = 0) const {
        if (duration == 0) return noise(t * rate + offset, seed);
        const double period = std::max(1.0, jsRound(rate * duration)), x = t / duration * period + offset,
                     i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
        const double a = rand({jsUint(wrap(i, period)), seed}), b = rand({jsUint(wrap(i + 1, period)), seed});
        return (a + (b - a) * u) * 2 - 1;
    }
    double operator()(double rate, uint32_t seed = 0, double offset = 0) const { return at(time, rate, seed, offset); }
};
struct Layer {
    double width, height;
    int    ramp;
};
constexpr std::array<Layer, 4> LAYERS{{{1, 1, 6}, {0.76, 0.86, 4}, {0.54, 0.68, 2}, {0.3, 0.42, 0}}};
constexpr std::array<Layer, 6> BIG_LAYERS{
    {{1, 1, 6}, {0.8, 0.9, 5}, {0.62, 0.78, 4}, {0.44, 0.6, 3}, {0.26, 0.4, 1}, {0.12, 0.22, 0}}};
constexpr std::array<Layer, 4> BACK_LAYERS{{{1, 1, 7}, {0.76, 0.86, 5}, {0.54, 0.68, 3}, {0.3, 0.42, 2}}};
template <class Make, class Draw>
void looped(double t, double D, int count, uint32_t seed, Make make, Draw draw) {
    for (int i = 0; i < count; ++i) {
        Rng          r(hash({seed, uint32_t(i)}));
        const double born = r.next() * D;
        const auto   q    = make(r, i);
        for (double age = wrap(t - born, D); age < q.life; age += D) draw(q, age, i);
    }
}
template <class N>
void flameTongue(Paint& p, double x, double y, double h, double w, double sx, N noiseAt, uint32_t n,
                 std::span<const uint32_t> H, std::span<const Layer> layers = LAYERS, double jitter = 0.5,
                 double wobble = 1.1) {
    if (h < 1) return;
    for (const auto& layer : layers) {
        const double   L = h * layer.height;
        const uint32_t c = H[size_t(layer.ramp)];
        for (int row = 0; row <= L; ++row) {
            const double r = row, u = r / std::max(1.0, L);
            const double half = w * layer.width * std::pow(1 - u, 0.85) * std::min(1.0, (r + 1.5) / 3.5) +
                                noiseAt(9, n, r * 0.45) * jitter;
            if (half < 0.3) continue;
            const double cx = x + sx * u * u * layer.height + noiseAt(4, n + 7, r * 0.2) * u * wobble,
                         Y  = jsRound(y - r);
            for (int px = int(jsRound(cx - half)), end = int(jsRound(cx + half)); px <= end; ++px) p.px(px, Y, c);
        }
    }
}
inline double pnoise(double x, double period, uint32_t seed = 0) {
    const double i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
    const double a = rand({jsUint(wrap(i, period)), seed}), b = rand({jsUint(wrap(i + 1, period)), seed});
    return (a + (b - a) * u) * 2 - 1;
}
template <class N>
void flameBlade(Paint& p, double x, double y, double ux, double uy, double len, double w, double bend, N noiseAt,
                uint32_t n, std::span<const uint32_t> H, std::span<const Layer> layers = LAYERS, double jit = 0.5,
                double wob = 1.1) {
    if (len < 1) return;
    const double nx = -uy, ny = ux;
    for (const auto& layer : layers) {
        const double L = len * layer.height;
        for (double r = 0; r <= L; r += 0.5) {
            const double u    = r / std::max(1.0, L);
            const double half = w * layer.width * std::pow(1 - u, 0.85) * std::min(1.0, (r + 1.5) / 3.5) +
                                noiseAt(9, n, r * 0.45) * jit;
            if (half < 0.3) continue;
            const double off = bend * u * u * layer.height + noiseAt(4, n + 7, r * 0.2) * u * wob,
                         cx = x + ux * r + nx * off, cy = y + uy * r + ny * off;
            for (double q = -half; q <= half; q += 0.5) p.px(cx + nx * q, cy + ny * q, H[size_t(layer.ramp)]);
        }
    }
}
inline void solid(Paint& p, double x, double y, double r, uint32_t c) {
    if (r < 0.6) {
        p.px(x, y, c);
        return;
    }
    const double cx = p.toX(x, y), cy = p.toY(x, y), r2 = r * r;
    for (int sy = int(std::floor(cy - r)); sy <= int(std::ceil(cy + r)); ++sy) {
        const double dy = sy + 0.5 - cy, s2 = r2 - dy * dy;
        if (s2 < 0) continue;
        const double s = std::sqrt(s2);
        for (int sx = int(std::ceil(cx - s - 0.5)); sx <= int(std::floor(cx + s - 0.5)); ++sx) p.put(sx, sy, c);
    }
}
inline void soft(Paint& p, double x, double y, double r, uint32_t c, double a) {
    if (a <= 0.004) return;
    if (r < 0.5) {
        p.blend(x, y, c, a);
        return;
    }
    const double cx = p.toX(x, y), cy = p.toY(x, y), r2 = r * r;
    for (int sy = int(std::floor(cy - r)); sy <= int(std::ceil(cy + r)); ++sy) {
        const double dy = sy + 0.5 - cy, s2 = r2 - dy * dy;
        if (s2 < 0) continue;
        const double s = std::sqrt(s2);
        for (int sx = int(std::ceil(cx - s - 0.5)); sx <= int(std::floor(cx + s - 0.5)); ++sx) p.blendPx(sx, sy, c, a);
    }
}
inline void firePuff(Paint& p, double x, double y, double k, double size, double fraction, std::span<const uint32_t> H,
                     double glow = 0.18, double tx = 0, double ty = -1) {
    const double   age = 1 - k, heat = std::min(0.999, std::pow(age, 0.62) + fraction * 0.1);
    const size_t   n = H.size(), index = std::min(n - 2, size_t(std::floor(heat * double(n - 1))));
    const double   r = size * (0.55 + 0.45 * std::min(1.0, age * 7)) * (1 - age * 0.55);
    const uint32_t c = H[index];
    if (index >= n - 2) {
        soft(p, x, y, r, c, std::min(1.0, k * 3) * 0.85);
        return;
    }
    solid(p, x, y, r, H[std::min(n - 1, index + 2)]);
    if (r > 1.6 && ty > -0.99) solid(p, x + tx * r * 1.5, y + ty * r * 1.5, r * 0.3, H[std::min(n - 1, index + 1)]);
    solid(p, x - r * 0.16, y - r * 0.22, r * 0.78, c);
    if (r > 1.6) solid(p, x + tx * r * 0.9, y + ty * r * 0.9, r * 0.42, c);
    if (index <= 3 && r > 1.8) solid(p, x - r * 0.28, y - r * 0.34, r * 0.36, H[index == 0 ? 0 : index - 1]);
    if (index < 6) p.glow(x, y, r * 2.4 + 2, c, glow * k);
}
inline void lick(Paint& p, double x, double y, double k, double size, std::span<const uint32_t> H) {
    const double r = size * (0.4 + 0.6 * k);
    solid(p, x, y, r + 0.6, k > 0.5 ? H[5] : H[6]);
    if (k > 0.35) solid(p, x, y - 0.3, r * 0.6, k > 0.7 ? H[2] : H[4]);
}
inline void smokePuff(Paint& p, double x, double y, double k, double size, uint32_t color = 0, bool big = false,
                      double alpha = 1) {
    const double   age = 1 - k, r = size * (0.65 + std::min(1.0, age * 2.5) * 0.45 + age * (big ? 0.5 : 0.4));
    const double   a = std::min(1.0, age * 12) * std::min(1.0, k * 2.4) * (big ? 0.7 : 0.58) * alpha;
    const uint32_t c = color ? mix(color, at(SMOKE_RAMP, age), age) : at(SMOKE_RAMP, age * 1.05);
    soft(p, x, y, r, c, a * 0.45);
    soft(p, x + r * 0.05, y + r * 0.06, r * 0.74, c, a * 0.62);
    if (age < 0.5) soft(p, x - r * 0.22, y - r * 0.3, r * 0.5, mix(c, WHITE, 0.12), a * 0.4 * (1 - age * 2));
}
inline void simDraw(Paint& p, const Particles& particles, std::span<const uint32_t> H, Paint::Layer layer) {
    const auto puffs = [&](bool back) {
        for (const auto& q : particles.list)
            if (q.kind == ParticleKind::Fire && q.delay <= 0 && hasFlag(q.flags, ParticleFlag::Back) == back)
                firePuff(p, q.x, q.y, q.life / q.max, q.size, std::fmod(q.seed, 1), H);
    };
    if (layer == Paint::Layer::Back) {
        particles.draw(p, Paint::Layer::Back, ParticleKind::Fire);
        puffs(true);
    } else {
        puffs(false);
        particles.draw(p, Paint::Layer::Front, ParticleKind::Fire);
    }
}
inline void ember(Paint& p, double x, double y, double k, double size, std::span<const uint32_t> cols, double phase) {
    if (k < 0.25 && std::sin(phase) < 0) return;
    const uint32_t c = k > 0.6 ? cols[0] : k > 0.3 ? cols[1] : cols[2];
    if (size >= 2) {
        const uint32_t rim = k > 0.45 ? cols[2] : cols[3];
        p.px(x - 1, y, rim);
        p.px(x + 1, y, rim);
        p.px(x, y - 1, rim);
        p.px(x, y + 1, rim);
        p.px(x, y, c);
        p.glow(x, y, 8, c, 0.4 * k);
        return;
    }
    p.px(x, y, c);
    if (size > 1.4) p.px(x, y + 1, cols[3]);
    p.glow(x, y, 3 + size, c, 0.35 * k);
}
inline void spark(Paint& p, double x, double y, double vx, double vy, double k, std::span<const uint32_t> cols) {
    const double   speed = jsHypot2(vx, vy), sp = speed == 0 ? 1 : speed, ux = vx / sp, uy = vy / sp,
                   len  = std::min(9.0, 1 + sp * 0.022);
    const uint32_t head = k > 0.6 ? cols[0] : k > 0.3 ? cols[1] : cols[2];
    p.line(x - ux * len, y - uy * len, x - ux * len * 0.4, y - uy * len * 0.4, k > 0.5 ? head : cols.back());
    p.line(x - ux * len * 0.45, y - uy * len * 0.45, x, y, head);
    p.glow(x, y, 3, head, 0.45 * k);
}
struct Motion {
    double x, y, vx, vy;
};
inline std::array<double, 2> along(double x, double v, double a, double k, double t) {
    if (k <= 0) return {x + v * t + 0.5 * a * t * t, v + a * t};
    const double e = std::exp(-k * t), vt = a / k;
    return {x + vt * t + (v - vt) * (1 - e) / k, vt + (v - vt) * e};
}
inline Motion drift(double x, double y, double vx, double vy, double t, double ax, double ay, double kx, double ky) {
    const auto X = along(x, vx, ax, kx, t), Y = along(y, vy, ay, ky, t);
    return {X[0], Y[0], X[1], Y[1]};
}
// Analytic damped trajectory with at most three floor impacts, matching the source bisection.
inline Motion sparkAt(double x, double y, double vx, double vy, double t, double floor = 0, double g = 427,
                      double k = 2.2) {
    Motion s{x, y, vx, vy};
    double left = t;
    for (int bounce = 0; bounce < 3; ++bounce) {
        const auto e = drift(s.x, s.y, s.vx, s.vy, left, 0, g, k, k);
        if (e.y < floor || s.y >= floor) return e;
        double lo = 0, hi = left;
        for (int i = 0; i < 16; ++i) {
            const double m = (lo + hi) / 2;
            if (drift(s.x, s.y, s.vx, s.vy, m, 0, g, k, k).y < floor)
                lo = m;
            else
                hi = m;
        }
        const auto c = drift(s.x, s.y, s.vx, s.vy, hi, 0, g, k, k);
        s            = {c.x, floor - 0.25, c.vx * 0.7, -std::abs(c.vy) * 0.45};
        left -= hi;
    }
    const auto e = drift(s.x, s.y, s.vx, s.vy, left, 0, g, k, k);
    return e.y < floor ? e : Motion{e.x, floor - 0.25, e.vx, 0};
}
inline void floorLight(Paint& p, double x, double y, double rx, double ry, uint32_t c, double k) {
    if (k <= 0.004 || rx < 1 || ry < 0.5) return;
    p.shader(x - rx, y - ry, x + rx, y + ry, [&](double px, double py, ShadeOutput& out) {
        const double dx = (px - x) / rx, dy = (py - y) / ry, d = dx * dx + dy * dy;
        if (d >= 1) return;
        const double f = 1 - std::sqrt(d);
        out.l          = c;
        out.k          = f * f * k;
    });
}
struct Tongue {
    double dx, y, height, width;
    bool   back;
};
struct BurnOptions {
    double                duration = 2, x = 0, y = 0, height = 20, lean = 0.1, spread = 6, glow = 1, glowRadius = 46;
    double                smoke = 12, puffs = 8, licks = 4, embers = 8, smokeSize = 3, wind = 3;
    std::array<double, 2> smokeLife{1.6, 2.6};
    uint32_t              seed = 1;
};
struct LoopParticle {
    double x = 0, y = 0, vx = 0, vy = 0, life = 0, size = 0, a = 0, b = 0;
};
template <class Mid>
void burn(Paint& p, double t, std::span<const Tongue> tongues, std::span<const uint32_t> H, const BurnOptions& o,
          Mid mid) {
    const double   D = o.duration, X = o.x, Y = o.y, HC = o.height, W = o.spread, G = o.glow;
    const uint32_t S = o.seed;
    const Noise    N{D, t};
    const auto     fla   = [&](double tt) { return 0.74 + 0.16 * N.at(tt, 7.3, S + 1) + 0.1 * N.at(tt, 19, S + 2); };
    const double   fl    = fla(t);
    const uint32_t smoke = mix(hex(0x5b5566), H[6], 0.25), glow = mix(mix(H[3], H[4], 0.5), WHITE, 0.25);
    const std::array<uint32_t, 4> emb{H[1], H[2], H[4], H[6]};
    struct State {
        double   dx, y, h, w, env;
        bool     back;
        uint32_t n;
    };
    std::vector<State> tt;
    tt.reserve(tongues.size());
    Rng random(hash({S, 77}));
    for (size_t i = 0; i < tongues.size(); ++i) {
        const auto&  q  = tongues[i];
        const double dx = q.dx + random.range(-0.3, 0.3), h = 2 + HC * q.height * random.range(0.92, 1.08),
                     w = q.width * random.range(0.92, 1.08);
        tt.push_back({dx, q.y, h, w, q.height, q.back, (S * 7 + uint32_t(i)) * 13});
    }
    const auto count  = [&](double rate) { return int(jsRound(rate * D)); };
    const auto lean   = [&](double time) { return N.at(time, 0.67, S + 90) * o.lean; };
    const auto height = [&](size_t i, double time) {
        const auto&  q    = tt[i];
        const double leap = std::max(0.0, N.at(time, 2.6, q.n + 70) - 0.55) * 1.4;
        return (q.h + 0.25 * q.h * N.at(time, 3.3, q.n) + q.h * leap) * (0.9 + 0.1 * fla(time));
    };
    const auto sweep = [&](size_t i, double time, double h) {
        return N.at(time, 2.2, tt[i].n + 40) * (1 + h * 0.08) + lean(time) * h * 0.9 - clamp(tt[i].dx * 0.3, -2.5, 2.5);
    };
    const auto tip = [&](size_t i, double time) {
        const double h = height(i, time);
        return Point{X + tt[i].dx + sweep(i, time, h) * 0.85, Y + tt[i].y - h * 0.88 + 3};
    };
    const auto tongueAt = [&](size_t i) {
        const auto&                  q      = tt[i];
        const double                 h      = height(i, t);
        const std::span<const Layer> layers = q.back      ? std::span<const Layer>(BACK_LAYERS)
                                              : q.w > 2.2 ? std::span<const Layer>(BIG_LAYERS)
                                                          : std::span<const Layer>(LAYERS);
        flameTongue(p, X + q.dx, Y + q.y, h, q.w, sweep(i, t, h), N, q.n, H, layers, 0.5, 1.1 + h * 0.04);
    };
    looped(
        t, D, count(o.smoke), S + 300,
        [&](Rng& r, int) {
            LoopParticle q;
            q.x    = r.range(-W, W) * 0.7;
            q.y    = -HC * 0.8 - r.range(0, 6);
            q.vx   = r.range(-2, 2);
            q.vy   = -r.range(10, 17);
            q.life = r.range(o.smokeLife[0], o.smokeLife[1]);
            q.size = r.range(0.7, 1.1) * o.smokeSize;
            return q;
        },
        [&](const LoopParticle& q, double age, int) {
            const auto m = drift(q.x, q.y, q.vx, q.vy, age, o.wind, -20, 1.6, 1);
            smokePuff(p, X + m.x + N.at(t - age, 0.8, S + 60) * (1.5 + 2.5 * age), Y + m.y, 1 - age / q.life, q.size,
                      smoke);
        });
    struct Puff {
        size_t i;
        double fire, smokeLife, ox, oy, vx, vy, size, fraction, life;
    };
    std::vector<std::array<double, 5>> puffs;
    looped(
        t, D, int(tt.size()) * count(o.puffs), S + 200,
        [&](Rng& r, int j) {
            Puff q;
            q.i               = size_t(j) % tt.size();
            q.fire            = r.range(0.14, 0.3);
            const bool smokes = r.isChance(0.45);
            q.smokeLife       = r.range(0.8, 1.4);
            q.ox              = r.range(-1, 1);
            q.oy              = r.range(0, 4);
            q.vx              = r.range(-4, 4);
            q.vy              = -r.range(15, 30);
            q.size            = r.range(0.8, 1.6) * (0.8 + 0.6 * tt[q.i].env);
            q.fraction        = r.next();
            q.life            = q.fire + (smokes ? 1.4 : 0);
            return q;
        },
        [&](const Puff& q, double age, int) {
            const auto pos = tip(q.i, t - age);
            const auto m   = drift(pos.x + q.ox, pos.y + q.oy, q.vx, q.vy, std::min(age, q.fire), 0, -95, 5, 3.2);
            if (age < q.fire) {
                puffs.push_back({m.x, m.y, 1 - age / q.fire, q.size, q.fraction});
                return;
            }
            if (age - q.fire > q.smokeLife) return;
            const auto end = drift(m.x, m.y, m.vx * 0.6, m.vy * 0.6 - 6, age - q.fire, o.wind, -16, 1.6, 1.2);
            smokePuff(p, end.x, end.y, 1 - (age - q.fire) / q.smokeLife, q.size * 0.7);
        });
    for (size_t i = 0; i < tt.size(); ++i)
        if (tt[i].back) tongueAt(i);
    for (size_t i = 0; i < tt.size(); ++i)
        if (!tt[i].back) tongueAt(i);
    mid(fl);
    looped(
        t, D, int(tt.size()) * count(o.licks), S + 220,
        [&](Rng& r, int j) {
            LoopParticle q;
            q.x    = double(size_t(j) % tt.size());
            q.vx   = r.range(-3, 3);
            q.vy   = -r.range(12, 22);
            q.size = r.range(0.9, 1.6) * (0.8 + 0.5 * tt[size_t(q.x)].env);
            q.life = 0.26;
            return q;
        },
        [&](const LoopParticle& q, double age, int) {
            const auto pos = tip(size_t(q.x), t - age);
            lick(p, pos.x + q.vx * age, pos.y - 2 + q.vy * age, 1 - age / q.life, q.size, H);
        });
    for (const auto& q : puffs) firePuff(p, q[0], q[1], q[2], q[3], q[4], H);
    looped(
        t, D, count(o.embers), S + 500,
        [&](Rng& r, int) {
            LoopParticle q;
            q.x    = r.range(-W, W);
            q.y    = -r.range(HC * 0.3, HC * 0.8);
            q.vx   = r.range(-15, 15);
            q.vy   = -r.range(25, 60);
            q.life = r.range(1.1, 2.4);
            q.size = r.isChance(0.15) ? 2 : r.range(1, 1.6);
            q.a    = r.range(0, 6.3);
            q.b    = r.range(0, 6.3);
            return q;
        },
        [&](const LoopParticle& q, double age, int) {
            const auto m = drift(q.x, q.y, q.vx, q.vy, age, 0, -30, 1.6, 1.6);
            ember(p, X + m.x + 7 * (std::sin(age * 2.3 + q.a) - std::sin(q.a)),
                  Y + m.y + 2 * (std::sin(age * 3 + q.b) - std::sin(q.b)), 1 - age / q.life, q.size, emb,
                  age * 40 + q.a);
        });
    if (G > 0) {
        const double radius = o.glowRadius, lx = X + lean(t) * 12;
        p.glow(lx, Y - HC * 0.35, radius * 1.5, glow, 0.22 * fl * G);
        p.glow(lx, Y - HC * 0.75, radius, glow, 0.34 * fl * G);
        p.glow(X, Y - HC * 0.2, radius * 0.32, H[2], 0.2 * fl * G);
        p.glow(X, Y - 2, 8, H[0], 0.14 * fl * G);
    }
}
}  // namespace eve::stylize::pixelfx::fire
