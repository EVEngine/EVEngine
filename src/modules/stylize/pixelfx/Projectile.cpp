// eve-hot-path
// Adapted from pixelfx projectiles/_lib.js cloud/blur (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Projectile.h"
#include <climits>
namespace eve::stylize::pixelfx::projectile {
namespace {
std::vector<float> blur(std::span<const float> src, int w, int h, int step, int radius) {
    std::vector<float> out(src.size());
    const int          n = step == 1 ? w : h, lines = step == 1 ? h : w;
    const double       k = 1.0 / (2 * radius + 1);
    for (int l = 0; l < lines; ++l) {
        const int base = step == 1 ? l * w : l;
        double    acc  = 0;
        for (int j = -radius; j <= radius; ++j)
            if (j >= 0 && j < n) acc += src[size_t(base + j * step)];
        for (int j = 0; j < n; ++j) {
            out[size_t(base + j * step)] = float(acc * k);
            const int a = j - radius, b = j + radius + 1;
            if (a >= 0) acc -= src[size_t(base + a * step)];
            if (b < n) acc += src[size_t(base + b * step)];
        }
    }
    return out;
}
}  // namespace
void cloud(Paint& p, std::span<const Puff> list, std::span<const uint32_t> R, std::span<const uint32_t> WR,
           double density, double maximum, double lighting) {
    if (list.empty()) return;
    int X0 = INT_MAX, Y0 = INT_MAX, X1 = INT_MIN, Y1 = INT_MIN;
    for (const auto& q : list) {
        X0 = std::min(X0, int(std::floor(q.x - q.r)));
        X1 = std::max(X1, int(std::ceil(q.x + q.r)));
        Y0 = std::min(Y0, int(std::floor(q.y - q.r)));
        Y1 = std::max(Y1, int(std::ceil(q.y + q.r)));
    }
    const int          W = X1 - X0 + 3, H = Y1 - Y0 + 3;
    std::vector<float> D(size_t(W) * H), L(D.size()), F(D.size());
    --X0;
    --Y0;
    for (const auto& q : list) {
        const double r = q.r, r2 = r * r;
        for (int y = int(std::floor(q.y - r)); y <= int(std::ceil(q.y + r)); ++y) {
            const double dy = y + .5 - q.y;
            for (int x = int(std::floor(q.x - r)); x <= int(std::ceil(q.x + r)); ++x) {
                const double dx = x + .5 - q.x, d2 = (dx * dx + dy * dy) / r2;
                if (d2 >= 1) continue;
                const double k = std::sqrt(1 - d2) * q.a;
                const size_t i = size_t(y - Y0) * W + size_t(x - X0);
                D[i]           = float(double(D[i]) + k);
                L[i]           = float(double(L[i]) + k * q.b);
                F[i]           = float(double(F[i]) + k * q.w * (.5 + .5 * clamp(.5 + dx / r)));
            }
        }
    }
    const auto   B = blur(blur(D, W, H, 1, 2), W, H, W, 2);
    const double n = double(R.size());
    p.shade(X0 + 1, Y0 + 1, X1 + 1, Y1 + 1, [&](double lx, double ly, int sx, int sy) {
        const int cx = int(std::floor(lx)) - X0, cy = int(std::floor(ly)) - Y0;
        if (cx < 1 || cy < 1 || cx >= W - 1 || cy >= H - 1) return;
        const size_t i = size_t(cy) * W + size_t(cx);
        const double d = D[i];
        if (d < .05) return;
        const double a = std::min(maximum, std::ceil((1 - std::exp(-d * density)) * 6) / 6);
        if (a < .15) return;
        const double gy = double(B[i + W]) - double(B[i - W]), gx = double(B[i + 1]) - double(B[i - 1]);
        const double up = clamp(.5 + std::atan((gy * .9 + gx * .35) / (double(B[i]) * .16 + .03)) * .42 * lighting);
        const double w  = double(F[i]) / d;
        const size_t j =
            size_t(clamp(jsRound((double(L[i]) / d) * (.3 + .7 * up) * (n - 1) + .25 + w * 1.5), 0, n - 1));
        p.blendPx(sx, sy, w > .03 ? mix(R[j], WR[j], std::min(.9, std::ceil(w * 10) / 10)) : R[j], a);
    });
}

void Smoke::add(Rng& rng, SmokeParticle q) {
    q.seed = rng.next() * 100;
    list.push_back(q);
}
void Smoke::update(double dt) {
    time += dt;
    for (size_t remaining = list.size(); remaining > 0; --remaining) {
        const size_t i = remaining - 1;
        auto&        q = list[i];
        if (q.delay > 0) {
            q.delay -= dt;
            continue;
        }
        q.age += dt;
        if (q.age >= q.life) {
            list.erase(list.begin() + std::ptrdiff_t(i));
            continue;
        }
        if (q.curl != 0) {
            const double c = std::cos(q.curl * dt), s = std::sin(q.curl * dt), vx = q.vx;
            q.vx = vx * c - q.vy * s;
            q.vy = vx * s + q.vy * c;
        }
        q.vy -= q.rise * dt;
        q.vx += std::sin(time * 1.3 + q.seed) * 8 * dt;
        q.vx -= q.vx * q.drag * dt;
        q.vy -= q.vy * q.drag * dt;
        q.x += q.vx * dt;
        q.y += q.vy * dt;
    }
}
std::vector<Puff> Smoke::puffs() const {
    std::vector<Puff> out;
    out.reserve(list.size());
    for (const auto& q : list) {
        if (q.delay > 0) continue;
        const double k = q.age / q.life;
        out.push_back({q.x, q.y, q.r0 + (q.r1 - q.r0) * (1 - std::exp(-q.grow * q.age)),
                       q.a * std::min(1.0, q.age * 30) * std::pow(1 - k, 1.3), q.b * (1 - k * .5),
                       q.warm * std::exp(-q.age * 5)});
    }
    return out;
}
std::vector<Lobe> flashShape(Rng& r, int frame, const FlashOptions& o) {
    const double      k = frame == 1 ? 1 : .6, w = frame == 1 ? 1 : .72, S = o.size;
    std::vector<Lobe> L;
    L.push_back({.5, r.range(-.08, .08), r.range(5.5, 8) * S * k, r.range(2.1, 2.6) * S * w, .5 * o.core});
    L.push_back(
        {2, r.range(-.07, .07), r.range(o.spike[0], o.spike[1]) * S * k, r.range(1, 1.3) * S * w, .35 * o.core});
    auto ray = [&](double a, double len, double wid, bool on) {
        if (on) L.push_back({r.range(.5, 2), a, len * S * k, wid * S * w, .25 * o.core});
    };
    for (int side : {-1, 1}) {
        const double a = side * r.range(.3, .8), len = r.range(o.diagonal[0], o.diagonal[1]), wid = r.range(1.1, 1.5);
        const bool   on = r.isChance(.85);
        ray(a, len, wid, on);
        const double b = side * r.range(1.15, 1.7), len2 = r.range(o.cross[0], o.cross[1]), wid2 = r.range(1, 1.3);
        const bool   on2 = r.isChance(.6);
        ray(b, len2, wid2, on2);
        const double c = side * r.range(2, 2.7), len3 = r.range(o.back[0], o.back[1]);
        const bool   on3 = r.isChance(.7) && frame == 1;
        ray(c, len3, 1, on3);
    }
    return L;
}
void flash(Paint& p, double x, double y, double a, std::span<const Lobe> L, const std::array<uint32_t, 4>& C,
           uint32_t seed) {
    const double c = std::cos(a), s = std::sin(a);
    double       ext = 2;
    for (const auto& l : L) ext = std::max(ext, l.off + l.length + 2);
    p.shade(x - ext, y - ext, x + ext, y + ext, [&](double px, double py, int sx, int sy) {
        const double dx = px - x, dy = py - y, along = dx * c + dy * s, across = -dx * s + dy * c;
        double       m   = -9;
        bool         hot = false, warm = false;
        for (const auto& l : L) {
            const double ca = std::cos(l.angle), sa = std::sin(l.angle), lu = (along - l.off) * ca + across * sa,
                         lv = -(along - l.off) * sa + across * ca;
            if (lu < -1.5 || lu > l.length + 1) continue;
            const double k  = std::max(0.0, lu) / l.length,
                         r  = l.width * std::pow(std::max(0.0, 1 - k), .8) * std::min(1.0, .45 + k * 3),
                         mm = r - std::abs(lv);
            if (mm > m) m = mm;
            if (lu < l.length * l.core && std::abs(lv) <= r * .5) hot = true;
            if (mm > r * .4) warm = true;
        }
        const double dm = 1.6 - jsHypot2(along - .5, across);
        if (dm > m) m = dm;
        if (dm > .6) hot = true;
        m += (rand({jsUint(std::floor(px) * 73 + std::floor(py) * 151), seed}) - .5) * .5 * std::min(1.0, ext / 14);
        if (m >= -.85) p.put(sx, sy, m < 0 ? C[3] : hot ? C[0] : warm ? C[1] : C[2]);
    });
}
Casing casing(double x, double y, double vx, double vy, double va, double age, double g, double floor, double bounce,
              double a) {
    double t0 = 0, hit = -1;
    for (int n = 0; n < 6; ++n) {
        const double d = floor - y, T = (-vy + std::sqrt(std::max(0.0, vy * vy + 2 * g * d))) / g;
        if (age - t0 < T) {
            const double u = age - t0;
            return {x + vx * u, y + vy * u + .5 * g * u * u, a + va * u, false, hit};
        }
        x += vx * T;
        a += va * T;
        t0 += T;
        hit              = age - t0;
        const double vin = vy + g * T;
        y                = floor;
        if (vin * bounce < 45) {
            const double u = age - t0, k = 7, s = (1 - std::exp(-k * u)) / k;
            return {x + vx * .6 * s, floor, jsRound(a / PI) * PI, true, hit};
        }
        vy = -vin * bounce;
        vx *= .55;
        va *= .6;
    }
    return {x, floor, jsRound(a / PI) * PI, true, hit};
}
void drawCasing(Paint& p, Casing k, std::span<const uint32_t> B, double fade) {
    const double ca = std::cos(k.angle), sa = std::sin(k.angle), x = k.x, y = k.y - .5;
    p.dot(x - ca, y - sa, B[1], fade);
    p.dot(x, y, B[2], fade);
    p.dot(x + ca, y + sa, B[3], fade);
    if (k.hit >= 0 && k.hit < .07) {
        p.px(x + ca * .5, y - 1, WHITE);
        p.glow(x, y, 5, B[3], .55 * (1 - k.hit / .07));
    }
}
}  // namespace eve::stylize::pixelfx::projectile
