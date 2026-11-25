// eve-hot-path
// Native adaptation of pixelfx shadow/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
// Sprite Fusion black-hole, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#pragma once
#include <deque>
#include <type_traits>
#include "stylize/pixelfx/Shadow.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::shadow {
/** @brief Owning source ink trajectory; all values belong to its effect instance. */
struct Drop {
    double x, y, vx, vy, size;
};
/** @brief Source ink trajectories with injected time, floor and synchronous landing observation.
 * @note Caller-thread only; callbacks must not mutate/reenter this collection and are never retained.
 * @cost Storage linear in emitted live drops; update visits each drop once plus erasure shifts. */
struct Drops {
    std::vector<Drop> list;
    double            gravity = .8;
    /** @brief Append an owning trajectory. @cost Amortized vector growth per emitted drop. */
    void add(Drop d) { list.push_back(d); }
    /** @brief Advance in source reverse insertion order, reporting impact before erasure. */
    template <class Floor, class Land>
    void update(double dt, Floor floor, Land onLand) {
        static_assert(std::is_void_v<std::invoke_result_t<Land&, double, double, double, double>>,
                      "Ink landing observers must return void; critical results must not be discarded");
        for (size_t i = list.size(); i-- > 0;) {
            auto& d = list[i];
            d.vy += GRAVITY * gravity * dt;
            d.vx -= d.vx * .4 * dt;
            d.x += d.vx * dt;
            d.y += d.vy * dt;
            const double f = floor(d.x);
            if (d.vy > 0 && d.y >= f) {
                onLand(d.x, f, d.vy, d.size);
                list.erase(list.begin() + std::ptrdiff_t(i));
            }
        }
    }
    /** @brief Draw source up/down drop highlights; painter is a synchronous borrow.
     * @cost At most three writes per live drop, without geometry allocation. */
    void draw(Paint& p, uint32_t body, uint32_t shine) const {
        for (const auto& d : list) {
            const bool up = d.vy < 0;
            p.px(d.x, d.y, body);
            if (d.size > 1.4) p.px(d.x, d.y + (up ? 1 : -1), body);
            p.px(d.x, d.y - (d.size > 1.4 && !up ? 1 : 0), shine);
        }
    }
};
enum class PuffKind { Flame, Soot };
struct Puff {
    PuffKind kind;
    double   x, y, vx, vy, life, max, size, delay, seed;
    bool     smokes;
};
struct PuffLight {
    double x, y, strength, reach;
};
/** @brief Source flame/side-lit soot simulation; owns particles, ramps and clock.
 * @note Caller-thread only. RNG, floor, painter and light are synchronous borrows, never retained.
 * @cost Storage linear in live emissions; update/draw visit each live puff once. */
struct Puffs {
    std::deque<Puff>      list;
    double                time = 0;
    std::vector<uint32_t> flame, soot, rim;
    uint32_t              cap = 0;
    /** @brief Append an owning source puff, consuming exactly one injected RNG sample. */
    void spawn(Rng& rng, PuffKind kind, double x, double y, double vx, double vy, double life, double size,
               double delay = 0, bool smokes = false) {
        list.push_back({kind, x, y, vx, vy, life, life, size, delay, rng.next() * 100, smokes});
    }
    /** @brief Advance source reverse-order dynamics; a dying flame may append soot for the next tick. */
    template <class Floor>
    void update(double dt, Rng& rng, Floor floor) {
        time += dt;
        for (size_t i = list.size(); i-- > 0;) {
            auto& q = list[i];
            if (q.delay > 0) {
                q.delay -= dt;
                continue;
            }
            if ((q.life -= dt) <= 0) {
                if (q.kind == PuffKind::Flame && q.smokes) {
                    const double life = rng.range(.9, 1.6);
                    spawn(rng, PuffKind::Soot, q.x, q.y - 1, q.vx * .5, q.vy * .5 - 6, life, q.size * 1.25);
                }
                list.erase(list.begin() + static_cast<std::deque<Puff>::difference_type>(i));
                continue;
            }
            if (q.kind == PuffKind::Flame) {
                q.vy -= 95 * dt;
                q.vx += std::sin(time * 17 + q.seed) * 30 * dt;
                q.vx -= q.vx * 5 * dt;
                q.vy -= q.vy * 3.2 * dt;
            } else {
                q.vy -= 18 * dt;
                q.vx += std::sin(time * 1.3 + q.seed) * 8 * dt;
                q.vx -= q.vx * 1.6 * dt;
                q.vy -= q.vy * 1.3 * dt;
            }
            q.x += q.vx * dt;
            q.y += q.vy * dt;
            if constexpr (!std::is_same_v<Floor, std::nullptr_t>) {
                if (q.kind == PuffKind::Soot) {
                    const double f = floor(q.x) - q.size * .6;
                    if (q.y > f) {
                        q.y  = f;
                        q.vy = std::min(0., q.vy);
                        q.vx *= 1 - 2 * dt;
                    }
                }
            }
        }
    }
    /** @brief Draw one source puff kind without mutating simulation; optional light is borrowed for this call. */
    void draw(Paint& p, PuffKind kind, const PuffLight* light = nullptr) const {
        const int n = int(flame.size());
        for (const auto& q : list) {
            if (q.delay > 0 || q.kind != kind) continue;
            const double k = q.life / q.max, age = 1 - k, x = q.x, y = q.y;
            if (q.kind == PuffKind::Flame) {
                const double heat = std::min(.999, std::pow(age, .62) + std::fmod(q.seed, 1.) * .1);
                const int    idx  = std::min(n - 2, int(std::floor(heat * (n - 1))));
                const double r    = q.size * (.55 + .45 * std::min(1., age * 7)) * (1 - age * .55);
                const auto   c    = flame[size_t(idx)];
                if (idx >= n - 3)
                    p.disc(x, y, r, c, std::min(1., k * 3) * .8);
                else {
                    p.circle(x, y, r, flame[size_t(std::min(n - 1, idx + 2))]);
                    p.circle(x - r * .16, y - r * .22, r * .78, c);
                    if (r > 1.6) p.circle(x, y - r * .9, r * .42, c);
                    if (idx <= 3 && r > 1.8)
                        p.circle(x - r * .28, y - r * .34, r * .36, flame[size_t(std::max(0, idx - 1))]);
                    if (idx < 6) p.glow(x, y, r * 2.4 + 2, c, .2 * k);
                }
                continue;
            }
            const double r    = q.size * (.65 + std::min(1., age * 2.5) * .45 + age * .45);
            const double a    = std::min(1., age * 10) * std::min(1., k * 2.2) * .72;
            const auto   body = mix(soot[0], soot[1], age);
            const double edge = a * .5, core = 1 - (1 - a * .5) * (1 - a * .62);
            p.disc(x, y + r * .06, r, body, [&](double d, double) { return d < .72 ? core : edge; });
            const double lk = light ? light->strength : 0;
            if (lk < .5) p.disc(x - r * .2, y - r * .32, r * .48, cap, a * .32 * (1 - age * .7) * (1 - lk * 2));
            if (lk <= .02) continue;
            const double lx = light->x - x, ly = light->y - y, root = std::sqrt(lx * lx + ly * ly),
                         dl = root ? root : 1;
            const double ux = lx / dl, uy = ly / dl, lit = std::min(1., lk * clamp(1.3 - dl / light->reach)) * a;
            if (lit <= .02) continue;
            p.disc(x + ux * r * .26, y + uy * r * .26, r * .72, rim[2], lit * .55);
            p.disc(x + ux * r * .5, y + uy * r * .5, r * .46, rim[1], lit * .5);
            if (r > 3) p.disc(x + ux * r * .68, y + uy * r * .68, r * .24, rim[0], lit * .4);
        }
    }
};
}  // namespace eve::stylize::pixelfx::shadow
