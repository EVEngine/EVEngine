// Native adaptation of pixelfx holy/_lib.js loose feathers, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::holy {
/** @brief Draw one source feather using synchronous painter/ramp borrows. */
inline void feather(Paint& p, double x, double y, double angle, double length, double width,
                    std::span<const uint32_t> R, double alpha = 1, double side = 1, double hot = 0,
                    double quill = .02) {
    if (length < 1.5 || alpha <= .02) return;
    const size_t n  = R.size();
    const double ca = std::cos(angle), sa = std::sin(angle), x1 = x + ca * length, y1 = y + sa * length,
                 pad  = width + 1.2;
    const bool   h    = hot > .5;
    const auto   edge = h ? R[n - 3] : R[n - 5], lead = h ? WHITE : R[n - 2];
    const std::array<uint32_t, 3> B =
        h ? std::array<uint32_t, 3>{WHITE, WHITE, R[n - 2]} : std::array<uint32_t, 3>{R[n - 2], R[n - 3], R[n - 4]};
    p.shade(std::min(x, x1) - pad, std::min(y, y1) - pad, std::max(x, x1) + pad, std::max(y, y1) + pad,
            [&](double px, double py, int sx, int sy) {
                const double dx = px - x, dy = py - y, u = (dx * ca + dy * sa) / length;
                if (u < 0 || u > 1.05) return;
                const double v = (-dx * sa + dy * ca) * side, av = std::abs(v),
                             w   = width * std::sqrt(clamp((u - quill) / .22)) *
                                   std::sqrt(std::max(0., 1 - std::pow(std::max(0., (u - .55) / .47), 2))),
                             lim = v < 0 ? w * .42 : w, fade = u < .72 ? 1 : 1 - (u - .72) * 1.6;
                if (av < .5 && u < .9) {
                    p.put(sx, sy, u < .5 ? WHITE : B[0], alpha);
                    return;
                }
                if (av > lim + .05 || w < .5) return;
                if (lim - av < 1) {
                    if (v > 0)
                        p.put(sx, sy, u > .85 ? B[2] : edge, alpha * fade);
                    else
                        p.put(sx, sy, lead, alpha);
                    return;
                }
                if (u < .72) {
                    p.put(sx, sy, u < .38 ? B[0] : B[1], alpha);
                    return;
                }
                p.blendPx(sx, sy, B[2], alpha * fade);
            });
}
/** @brief Owning source feather launch; optional overrides still consume the source default RNG draws. */
struct FeatherLaunch {
    double                x, y, vx, vy, angle, length, width, life;
    double                delay = 0;
    bool                  back  = false;
    std::optional<double> sink, amplitude;
    std::optional<double> side;
};
/** @brief Owning mutable source feather state with injected updates, no retained RNG/painter references. */
struct Feather {
    double x, y, vx, vy, angle, length, width, life, delay;
    bool   back;
    double side, phase, omega, amplitude, sink, angularVelocity, tilt, maximum, age = 0;
    bool   landed = false;
};
/** @brief Caller-thread affine source feather collection, updated using caller-supplied dt and RNG. */
struct Feathers {
    std::vector<Feather> list;
    double               floor;
    /** @brief Create an empty collection with an explicit floor coordinate. */
    explicit Feathers(double floorY = 0) : floor(floorY) {}
    /** @brief Copy launch data and consume the caller's named source RNG stream synchronously. */
    void add(Rng& r, const FeatherLaunch& f) {
        const double side = r.sign(), phase = r.range(0, TAU), omega = r.range(3.2, 4.6), amp = r.range(6, 11),
                     sink = r.range(14, 24), va = r.range(-9, 9), tilt = r.range(-.35, .35);
        list.push_back({f.x, f.y, f.vx, f.vy, f.angle, f.length, f.width, f.life, f.delay, f.back,
                        f.side.value_or(side), phase, omega, f.amplitude.value_or(amp), f.sink.value_or(sink), va, tilt,
                        f.life});
    }
    /** @brief Advance the source simulation in reverse erase order; no external callbacks or state retained. */
    void update(double dt) {
        for (size_t i = list.size(); i > 0; --i) {
            auto& f = list[i - 1];
            if (f.delay > 0) {
                f.delay -= dt;
                continue;
            }
            f.life -= dt;
            if (f.life <= 0) {
                list.erase(list.begin() + std::ptrdiff_t(i - 1));
                continue;
            }
            f.age += dt;
            if (f.landed) {
                f.angle += (jsRound(f.angle / PI) * PI - f.angle) * std::min(1., dt * 10);
                continue;
            }
            f.vx -= f.vx * 3.4 * dt;
            f.vy -= f.vy * 3.4 * dt;
            const double s = smooth(.1, .6, f.age), ph = f.age * f.omega + f.phase, c = std::cos(ph);
            f.x += (f.vx + std::sin(ph) * f.amplitude * s * .9) * dt;
            f.y += (f.vy + f.sink * s * (c * c * 1.5 - .2)) * dt;
            const double rest = jsRound(f.angle / PI) * PI + f.tilt + std::sin(ph) * .6;
            f.angle += f.angularVelocity * dt * (1 - s) + (rest - f.angle) * std::min(1., dt * 5 * s);
            if (f.y >= floor - .5) {
                f.y      = floor - .5;
                f.landed = true;
                f.life   = std::min(f.life, .6);
            }
        }
    }
    /** @brief Draw a source layer with synchronous painter and material borrows. */
    void draw(Paint& p, std::span<const uint32_t> R, Paint::Layer layer) const {
        for (const auto& f : list) {
            if (f.delay > 0 || (f.back ? Paint::Layer::Back : Paint::Layer::Front) != layer) continue;
            const double k = f.life / f.maximum, a = std::min({1., f.life / .45, f.age / .06}), ca = std::cos(f.angle),
                         sa = std::sin(f.angle);
            feather(p, f.x - (ca * f.length) / 2, f.y - (sa * f.length) / 2, f.angle, f.length, f.width, R, a, f.side,
                    f.age < .06 ? 1 : 0, .2);
            p.glow(f.x, f.y, 4 + f.length * .5, R[R.size() - 3], .22 * a * (.4 + .6 * k));
        }
    }
};
}  // namespace eve::stylize::pixelfx::holy
