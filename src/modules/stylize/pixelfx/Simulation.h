// Native adaptation of pixelfx core/sim.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
// Internal helpers: the effect owns RNG/time. Callables passed to update are borrowed only
// for that synchronous call; no callbacks, actor storage or graphics pointers are retained.
#pragma once
#include <deque>
#include <optional>
#include "stylize/pixelfx/Painter.h"

namespace eve::stylize::pixelfx {
constexpr double                  GRAVITY = 950;
constexpr std::array<Point, 4>    CROSS{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
constexpr std::array<uint32_t, 4> HOT{{0xffa6f3ffu, 0xff6be2ffu, 0xff2a8affu, 0xff1d56f2u}};
constexpr std::array<uint32_t, 9> FIRE_RAMP{{0xffffffffu, 0xffa6f3ffu, 0xff6be2ffu, 0xff3dc2ffu, 0xff2a8affu,
                                             0xff1d56f2u, 0xff1f28c9u, 0xff171f8au, 0xff12174au}};
constexpr std::array<uint32_t, 5> SMOKE_RAMP{{0xff726f7bu, 0xff60565du, 0xff483d43u, 0xff30262bu, 0xff1e171au}};
enum class ParticleKind { Spark, Mote, Streak, Flake, Vapour, Dust, Star, Ember, Smoke, Fire, Brand, Grit };
enum class ParticleFlag : uint8_t { None = 0, Smokes = 1, Big = 2, Back = 4 };
constexpr ParticleFlag operator|(ParticleFlag a, ParticleFlag b) { return ParticleFlag(uint8_t(a) | uint8_t(b)); }
inline bool            hasFlag(ParticleFlag value, ParticleFlag flag) { return (uint8_t(value) & uint8_t(flag)) != 0; }
inline bool            isSoft(ParticleKind k) {
    return k == ParticleKind::Vapour || k == ParticleKind::Dust || k == ParticleKind::Smoke;
}
struct SimParticle {
    ParticleKind kind = ParticleKind::Spark;
    double       x = 0, y = 0, vx = 0, vy = 0, life = 1, max = 1, size = 1, seed = 0, delay = 0;
    uint32_t     color = WHITE;
    ParticleFlag flags = ParticleFlag::None;
};
struct Particles {
    // deque keeps the current particle valid when a FIRE death or BRAND trail appends particles.
    // Erasure is followed immediately by continue; callers do not keep element references.
    std::deque<SimParticle> list;
    double                  time = 0;
    std::vector<uint32_t>   sparks{HOT.begin(), HOT.end()};
    uint32_t                haze = 0;
    void spawn(Rng& rng, ParticleKind kind, double x, double y, double vx, double vy, double life,
               uint32_t color = WHITE, double size = 1, double delay = 0, ParticleFlag flags = ParticleFlag::None) {
        list.push_back({kind, x, y, vx, vy, life, life, size, rng.next() * 100, delay, color, flags});
    }
    template <class Ground, class Land>
    void update(double dt, Rng& R, Ground ground, Land onLand) {
        time += dt;
        for (size_t remaining = list.size(); remaining > 0; --remaining) {
            const size_t i = remaining - 1;
            auto&        p = list[i];
            if (p.delay > 0) {
                p.delay -= dt;
                continue;
            }
            p.life -= dt;
            if (p.life <= 0) {
                if (p.kind == ParticleKind::Fire && hasFlag(p.flags, ParticleFlag::Smokes)) {
                    const bool   big  = hasFlag(p.flags, ParticleFlag::Big);
                    const double life = big ? R.range(1.3, 2.3) : R.range(0.8, 1.4);
                    spawn(R, ParticleKind::Smoke, p.x, p.y, p.vx * 0.6, p.vy * 0.6 - 6, life, 0,
                          p.size * (big ? 0.85 : 0.7), 0, big ? ParticleFlag::Big : ParticleFlag::None);
                }
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            switch (p.kind) {
                case ParticleKind::Fire:
                    p.vy -= 95 * dt;
                    p.vx += std::sin(time * 17 + p.seed) * 30 * dt;
                    p.vx -= p.vx * 5 * dt;
                    p.vy -= p.vy * 3.2 * dt;
                    break;
                case ParticleKind::Grit: {
                    p.vy += GRAVITY * dt;
                    p.vx -= p.vx * 0.8 * dt;
                    const double g = ground(p.x), ny = p.y + p.vy * dt;
                    p.x += p.vx * dt;
                    if (p.vy > 0 && p.y <= g && ny >= g) {
                        p.y = g - 0.25;
                        if (p.vy > 130 && R.isChance(0.4)) {
                            p.vy = -p.vy * 0.25;
                            p.vx = p.vx * 0.5 + R.range(-10, 10);
                        } else {
                            p.vx   = 0;
                            p.vy   = 0;
                            p.life = std::min(p.life, 0.5);
                        }
                    } else
                        p.y = ny;
                    continue;
                }
                case ParticleKind::Brand: {
                    p.vy += GRAVITY * 0.8 * dt;
                    p.vx -= p.vx * 0.9 * dt;
                    if (R.isChance(0.9 * std::min(1.0, dt * 60))) {
                        const double vx = p.vx * 0.1 + R.range(-7, 7), life = R.range(0.16, 0.32),
                                     size = R.range(0.9, 1.7) * (0.5 + p.life / p.max * 0.6);
                        const auto   flag = R.isChance(0.4) ? ParticleFlag::Smokes : ParticleFlag::None;
                        spawn(R, ParticleKind::Fire, p.x, p.y, vx, p.vy * 0.1 - 10, life, 0, size, 0, flag);
                    }
                    const double g = ground(p.x), ny = p.y + p.vy * dt;
                    p.x += p.vx * dt;
                    if (p.vy > 0 && p.y <= g && ny >= g) {
                        onLand(p.x, g);
                        list.erase(list.begin() + std::ptrdiff_t(i));
                    } else
                        p.y = ny;
                    continue;
                }
                case ParticleKind::Spark: {
                    p.vy += GRAVITY * 0.45 * dt;
                    p.vx -= p.vx * 2.2 * dt;
                    p.vy -= p.vy * 2.2 * dt;
                    const double g = ground(p.x), ny = p.y + p.vy * dt;
                    p.x += p.vx * dt;
                    if (p.vy > 0 && p.y <= g && ny >= g) {
                        p.vy = -p.vy * 0.45;
                        p.vx *= 0.7;
                        p.y = g - 0.25;
                    } else
                        p.y = ny;
                    continue;
                }
                case ParticleKind::Streak:
                    p.vx += p.vx * 3 * dt;
                    p.vy += p.vy * 3 * dt;
                    break;
                case ParticleKind::Flake:
                    p.vx -= p.vx * 2.2 * dt;
                    p.vy -= p.vy * 2.2 * dt - 35 * dt;
                    p.x += std::sin(time * 2.5 + p.seed) * 7 * dt;
                    break;
                case ParticleKind::Ember:
                    p.vy += (-30 + std::sin(time * 3 + p.seed) * 20) * dt;
                    p.vx += std::cos(time * 2.3 + p.seed) * 45 * dt;
                    p.vx -= p.vx * 1.6 * dt;
                    p.vy -= p.vy * 1.6 * dt;
                    break;
                case ParticleKind::Smoke:
                    p.vy -= (hasFlag(p.flags, ParticleFlag::Big) ? 23 : 16) * dt;
                    p.vx += std::sin(time * 1.3 + p.seed) * 8 * dt;
                    p.vx -= p.vx * 1.6 * dt;
                    p.vy -= p.vy * 1.2 * dt;
                    break;
                case ParticleKind::Mote:
                    p.vx -= p.vx * 0.6 * dt;
                    p.vy -= p.vy * 0.6 * dt;
                    break;
                default: {
                    const double drag = isSoft(p.kind) ? 4 : 1.6;
                    p.vx -= p.vx * drag * dt;
                    p.vy -= p.vy * drag * dt + 5 * dt;
                    break;
                }
            }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
        }
    }
    void update(double dt, Rng& rng) {
        update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
    }
    void draw(Paint& p, std::optional<Paint::Layer> layer = std::nullopt,
              std::optional<ParticleKind> omit = std::nullopt) const {
        for (const auto& q : list) {
            if (q.delay > 0 || (omit && q.kind == *omit)) continue;
            const auto ownLayer =
                isSoft(q.kind) || hasFlag(q.flags, ParticleFlag::Back) ? Paint::Layer::Back : Paint::Layer::Front;
            if (layer && *layer != ownLayer) continue;
            const double k = q.life / q.max, x = q.x, y = q.y, speed = jsHypot2(q.vx, q.vy),
                         sp = speed == 0 ? 1 : speed, ux = q.vx / sp, uy = q.vy / sp;
            switch (q.kind) {
                case ParticleKind::Spark: {
                    const uint32_t head = k > 0.6 ? sparks[0] : k > 0.3 ? sparks[1] : sparks[2];
                    const double   len  = std::min(9.0, 1 + sp * 0.022);
                    p.line(x - ux * len, y - uy * len, x - ux * len * 0.4, y - uy * len * 0.4,
                           k > 0.5 ? head : sparks.back());
                    p.line(x - ux * len * 0.45, y - uy * len * 0.45, x, y, head);
                    p.glow(x, y, 3, head, 0.45 * k);
                    break;
                }
                case ParticleKind::Mote: {
                    const double len = std::min(16.0, 0.5 + sp * 0.026) * q.size, a = k < 0.35 ? k / 0.35 : 1;
                    p.line(x - ux * len, y - uy * len, x - ux * len * 0.35, y - uy * len * 0.35, scale(q.color, 0.55),
                           a);
                    p.line(x - ux * len * 0.4, y - uy * len * 0.4, x, y, q.color, a);
                    p.glow(x, y, 2.5 + q.size, q.color, 0.4 * k);
                    break;
                }
                case ParticleKind::Streak: {
                    const double L = clamp(sp / 30, 1, 7);
                    for (double d = L; d >= 0; d -= 0.5)
                        p.dot(x - ux * d, y - uy * d,
                              d < 1         ? WHITE
                              : d < L * 0.5 ? q.color
                                            : scale(q.color, 0.6),
                              d < L * 0.5 ? 1 : 1.2 - d / L);
                    p.glow(x, y, 4, q.color, 0.4);
                    break;
                }
                case ParticleKind::Flake: {
                    const double a = std::min(1.0, k * 3);
                    p.dot(x, y, q.color, a);
                    if (q.size > 1.3)
                        for (const auto& d : CROSS) p.dot(x + d.x, y + d.y, scale(q.color, 0.8), a * 0.8);
                    break;
                }
                case ParticleKind::Vapour: {
                    const double r = q.size * (0.6 + (1 - k) * 0.9);
                    p.disc(x, y, r, q.color, std::pow(k, 1.3) * 0.45);
                    if (haze) p.glow(x, y, r * 1.8, haze, 0.16 * k);
                    break;
                }
                case ParticleKind::Dust:
                    p.disc(x, y, q.size * (0.6 + (1 - k) * 0.9), q.color, std::pow(k, 1.3) * 0.7);
                    break;
                case ParticleKind::Smoke: {
                    const double   age = 1 - k;
                    const bool     big = hasFlag(q.flags, ParticleFlag::Big);
                    const double   r   = q.size * (0.65 + std::min(1.0, age * 2.5) * 0.45 + age * (big ? 0.5 : 0.4));
                    const double   a   = std::min(1.0, age * 12) * std::min(1.0, k * 2.4) * (big ? 0.7 : 0.58);
                    const uint32_t c   = q.color ? mix(q.color, at(SMOKE_RAMP, age), age) : at(SMOKE_RAMP, age * 1.05);
                    p.disc(x, y, r, c, a * 0.45);
                    p.disc(x + r * 0.05, y + r * 0.06, r * 0.74, c, a * 0.62);
                    if (age < 0.5)
                        p.disc(x - r * 0.22, y - r * 0.3, r * 0.5, mix(c, WHITE, 0.12), a * 0.4 * (1 - age * 2));
                    break;
                }
                case ParticleKind::Fire: {
                    const double age = 1 - k, heat = std::min(0.999, std::pow(age, 0.62) + std::fmod(q.seed, 1) * 0.1);
                    const size_t idx =
                        std::min(FIRE_RAMP.size() - 2, size_t(std::floor(heat * double(FIRE_RAMP.size() - 1))));
                    const double   r = q.size * (0.55 + 0.45 * std::min(1.0, age * 7)) * (1 - age * 0.55);
                    const uint32_t c = FIRE_RAMP[idx];
                    if (idx >= FIRE_RAMP.size() - 2)
                        p.disc(x, y, r, c, std::min(1.0, k * 3) * 0.85);
                    else {
                        p.circle(x, y, r, FIRE_RAMP[std::min(FIRE_RAMP.size() - 1, idx + 2)]);
                        p.circle(x - r * 0.16, y - r * 0.22, r * 0.78, c);
                        if (r > 1.6) p.circle(x, y - r * 0.9, r * 0.42, c);
                        if (idx <= 3 && r > 1.8)
                            p.circle(x - r * 0.28, y - r * 0.34, r * 0.36, FIRE_RAMP[idx == 0 ? 0 : idx - 1]);
                        if (idx < 6) p.glow(x, y, r * 2.4 + 2, c, 0.18 * k);
                    }
                    break;
                }
                case ParticleKind::Brand:
                    p.circle(x, y, 1.6, HOT[3]);
                    p.px(x, y, std::sin(time * 40 + q.seed) > 0 ? HOT[0] : HOT[1]);
                    p.px(x + 1, y, HOT[1]);
                    p.glow(x, y, 7, HOT[2], 0.45);
                    break;
                case ParticleKind::Grit:
                    if (k > 0.25 || k * 4 > 0.5) {
                        p.px(x, y, q.color);
                        if (q.size >= 2) {
                            p.px(x + 1, y, q.color);
                            p.px(x, y + 1, scale(q.color, 0.8));
                            p.px(x + 1, y + 1, scale(q.color, 0.7));
                        }
                    }
                    break;
                case ParticleKind::Ember: {
                    const uint32_t c = k > 0.6 ? sparks[0] : k > 0.3 ? sparks[1] : sparks[2];
                    if (k < 0.25 && std::sin(time * 40 + q.seed) < 0) break;
                    p.px(x, y, c);
                    if (q.size > 1.5) p.px(x, y + 1, sparks.back());
                    p.glow(x, y, 3 + q.size, c, 0.35 * k);
                    break;
                }
                case ParticleKind::Star: {
                    const double tw = 0.6 + 0.4 * std::sin(time * (8 + std::fmod(q.seed, 5)) + q.seed);
                    if (q.size < 1) {
                        if (tw * k > 0.25) p.px(x, y, tw > 0.8 ? WHITE : q.color);
                        p.glow(x, y, 3, q.color, 0.4 * k * tw);
                    } else
                        p.sparkle(x, y, q.size * tw * std::min(1.0, k * 3), q.color, scale(q.color, 0.75),
                                  0.5 * k * tw);
                    break;
                }
            }
        }
    }
};
struct Blaze {
    double x, y, life, max, acc, power;
};
struct Blazes {
    std::vector<Blaze> list;
    void               add(Rng& R, double x, double y, double life, double power) {
        list.push_back({x, y, life, life, R.next(), power});
    }
    void update(double dt, Particles& ps, Rng& R) {
        for (size_t left = list.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        b = list[i];
            b.life -= dt;
            if (b.life <= 0) {
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            const double k = b.life / b.max, intensity = b.power * (k > 0.7 ? 1 : 0.35 + k / 0.7 * 0.65);
            b.acc += dt * (6 + 16 * intensity);
            for (; b.acc > 1; b.acc--) {
                const double s = R.range(1.2, 2.8) * intensity, x = b.x + R.range(-1.5, 1.5), vx = R.range(-6, 6),
                             vy = R.range(-30, -12), life = R.range(0.28, 0.6);
                const auto   flags = ParticleFlag::Back | (R.isChance(0.3) ? ParticleFlag::Smokes : ParticleFlag::None);
                ps.spawn(R, ParticleKind::Fire, x, b.y - s * 0.6, vx, vy, life, 0, s, 0, flags);
                if (R.isChance(0.06)) {
                    const double evx = R.range(-15, 15), evy = R.range(-45, -15), elife = R.range(0.6, 1.4);
                    ps.spawn(R, ParticleKind::Ember, b.x, b.y - 1.5, evx, evy, elife, HOT[2], 1, 0, ParticleFlag::Back);
                }
            }
        }
    }
    void drawBack(Paint& p) const {
        for (const auto& b : list) {
            const double k = b.life / b.max;
            p.px(b.x, b.y, k > 0.4 ? (std::sin(b.life * 31 + b.x) > 0 ? HOT[1] : HOT[2]) : HOT[3]);
            p.glow(b.x, b.y, 6 + 5 * k * b.power, HOT[2], 0.3 * k * b.power);
        }
    }
};
enum class DebrisKind { Shard, Chunk, Drip };
struct Debris {
    DebrisKind kind = DebrisKind::Shard;
    double   x = 0, y = 0, vx = 0, vy = 0, angle = 0, angularVelocity = 0, size = 1, gravity = 1, drag = 0, bounce = 0,
             life = 1, max = 1;
    uint32_t color  = WHITE;
    bool     rest   = false;
    bool     dusted = false;
    std::vector<uint32_t> ramp;
};
template <class Body = Debris>
struct BasicBits {
    std::vector<Body> list;
    void              add(Body b) {
        b.max = b.life;
        list.push_back(std::move(b));
    }
    template <class Ground>
    void update(double dt, Ground ground) {
        for (size_t left = list.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        b = list[i];
            b.life -= dt;
            if (b.life <= 0) {
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (b.rest) continue;
            b.vy += GRAVITY * b.gravity * dt;
            b.vx -= b.vx * b.drag * dt;
            b.vy -= b.vy * b.drag * dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.angle += b.angularVelocity * dt;
            const double floor = ground(b.x), sp = b.vy;
            if (b.gravity <= 0 || b.y < floor || sp <= 0) continue;
            b.y = floor - 0.01;
            if (b.kind == DebrisKind::Drip)
                b.life = 0;
            else if (b.bounce > 0 && sp > 70) {
                b.vy = -sp * b.bounce;
                b.vx *= 0.7;
                b.angularVelocity *= 0.6;
            } else {
                b.rest = true;
                b.vx   = 0;
                b.vy   = 0;
            }
        }
    }
    void update(double dt) {
        update(dt, [](double) { return INFINITY; });
    }
    void draw(Paint& p) const {
        constexpr std::array<uint32_t, 5> defaultRamp{0xff30201cu, 0xff664a3au, 0xffa8866eu, 0xffdcc0a8u, 0xfffff2e8u};
        for (const auto& b : list) {
            const double alpha = std::min(1.0, b.life / 0.4), ca = std::cos(b.angle), sa = std::sin(b.angle);
            if (b.kind == DebrisKind::Shard)
                for (double d = -b.size / 2; d <= b.size / 2; d += 0.5)
                    p.dot(b.x + ca * d, b.y + sa * d, d > b.size / 2 - 0.6 ? WHITE : b.color, alpha);
            else if (b.kind == DebrisKind::Drip) {
                p.dot(b.x, b.y, b.color, alpha);
                p.dot(b.x, b.y - 1, b.color, alpha * 0.5);
            } else {
                const std::span<const uint32_t> R =
                    b.ramp.empty() ? std::span<const uint32_t>(defaultRamp) : std::span<const uint32_t>(b.ramp);
                const size_t n = R.size();
                const double x = jsRound(b.x), y = jsRound(b.y);
                const int    r = int(std::ceil(b.size)) + 1;
                for (int py = -r; py <= r; ++py)
                    for (int px = -r; px <= r; ++px) {
                        const double u = (px + 0.5) * ca + (py + 0.5) * sa, v = -(px + 0.5) * sa + (py + 0.5) * ca,
                                     d = std::abs(u) / b.size + std::abs(v) / (b.size * 0.62);
                        if (d > 1.3) continue;
                        p.dot(x + px, y + py,
                              d > 1   ? R[0]
                              : v < 0 ? (u < 0 ? R[n - 1] : R[n - 2])
                              : u < 0 ? R[n - 3]
                                      : R[std::max(size_t(1), n - 4)],
                              alpha);
                    }
            }
        }
    }
};
using Bits = BasicBits<>;

struct SimLight {
    double   x, y, radius;
    uint32_t color;
    double   strength, life, max;
};
struct Lights {
    std::vector<SimLight> list;
    void                  add(double x, double y, double r, uint32_t color, double k, double life) {
        list.push_back({x, y, r, color, k, life, life});
    }
    void update(double dt) {
        for (size_t i = list.size(); i > 0; --i) {
            auto& l = list[i - 1];
            l.life -= dt;
            if (l.life <= 0) list.erase(list.begin() + std::ptrdiff_t(i - 1));
        }
    }
    void draw(Paint& p) const {
        for (const auto& l : list) {
            const double k = l.life / l.max;
            p.glow(l.x, l.y, l.radius * (0.55 + 0.45 * k), l.color, l.strength * k * k);
        }
    }
};
/** @brief Draw a trusted authored eight-ray flare using a synchronously borrowed prepared ramp.
 * @cost Raster work scales with ray length per explicit draw; no input references survive.
 * @note Caller-thread affine; no callbacks or global mutable state. */
inline void flare(Paint& p, double x, double y, double L, double k, std::span<const uint32_t> ramp) {
    if (L < 1) return;
    const size_t n = ramp.size();
    for (int i = 0; i < 8; ++i) {
        const double angle = i * PI / 4,
                     len   = i % 4 == 0   ? L
                             : i % 2 == 0 ? L * 0.6
                                          : L * 0.3,
                     ca = std::cos(angle), sa = std::sin(angle);
        for (double d = 0; d <= len; d += 0.5) {
            const double u = d / len;
            p.dot(x + ca * d, y + sa * d,
                  u < 0.35  ? WHITE
                  : u < 0.7 ? ramp[n - 2]
                            : ramp[n - 3],
                  u < 0.6 ? 1 : k * 1.5 * (1 - u) + 0.2);
        }
    }
}
template <class Keep>
void band(Paint& p, double cx, double cy, double r, double sy, double th, double k, std::span<const uint32_t> ramp,
          Keep keep, bool hot = false) {
    if (r <= 0 || k <= 0) return;
    const size_t n = ramp.size();
    const int    m = int(std::ceil(r * TAU * 1.2));
    for (int j = 0; j < m; ++j) {
        const double a = double(j) / m * TAU, ca = std::cos(a), sa = std::sin(a);
        if (!keep(a)) continue;
        for (double o = th; o >= 0; o -= 0.5) {
            const double   u = o / th;
            const uint32_t c = u < 0.15  ? (hot ? WHITE : ramp[n - 1])
                               : u < 0.4 ? ramp[n - 2]
                               : u < 0.7 ? ramp[n - 3]
                                         : ramp[n - 4];
            p.dot(cx + ca * (r - o), cy + sa * (r - o) * sy, c, k * (u < 0.4 ? 1.2 : 1.15 - u));
        }
    }
}
struct CrystalOptions {
    double alpha = 1;
    bool   white = false;
    double glint = -1;
};
inline void crystal(Paint& p, double x, double base, double h, double w, double lean, std::span<const uint32_t> ramp,
                    const CrystalOptions& opts = {}) {
    if (h < 1.5) return;
    const int    n   = int(ramp.size());
    const auto   C   = [&](int i) { return ramp[size_t(std::max(0, n - 1 - i))]; };
    const double top = base - h, ridge0 = x - w * 0.14;
    const auto   inside = [&](int px, int py) {
        const double u = (base - (py + 0.5)) / h;
        return u >= 0 && u <= 1 && std::abs(px + 0.5 - (x + lean * u)) <= w / 2 * (1 - u) + 0.35;
    };
    for (int py = int(std::floor(top)) - 1; py < int(std::ceil(base)); ++py)
        for (int px = int(std::floor(x - w / 2 - std::abs(lean) - 2));
             px <= int(std::ceil(x + w / 2 + std::abs(lean) + 2)); ++px) {
            if (!inside(px, py)) {
                if (inside(px - 1, py) || inside(px + 1, py) || inside(px, py + 1) ||
                    (inside(px, py - 1) && py < base - 1))
                    p.dot(px, py, ramp[0], opts.alpha);
                continue;
            }
            if (opts.white) {
                p.dot(px, py, WHITE, opts.alpha);
                continue;
            }
            const double u = (base - (py + 0.5)) / h, rx = ridge0 + (x + lean - ridge0) * u, c = x + lean * u,
                         hw = w / 2 * (1 - u) + 0.35, fx = px + 0.5;
            uint32_t     col = u > 0.9                    ? WHITE
                               : std::abs(fx - rx) < 0.55 ? C(1)
                               : fx < rx           ? (fx - (c - hw) < 1                                       ? C(1)
                                                      : (fx - (c - hw)) / std::max(1.0, rx - (c - hw)) > 0.55 ? C(2)
                                                                                                              : C(3))
                               : c + hw - fx < 1.2 ? C(std::min(n - 2, 6))
                               : (fx - rx) / std::max(1.0, c + hw - rx) > 0.5 ? C(5)
                                                                              : C(4);
            if (opts.glint >= 0 && fx <= rx + 0.6) {
                const double g = std::abs(u - opts.glint);
                if (g < 0.05)
                    col = WHITE;
                else if (g < 0.1)
                    col = C(1);
            }
            p.dot(px, py, col, opts.alpha);
        }
}
}  // namespace eve::stylize::pixelfx
