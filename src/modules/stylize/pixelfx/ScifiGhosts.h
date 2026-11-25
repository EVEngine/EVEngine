// Native adaptation of pixelfx scifi/_lib.js loop particle reconstruction, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include <optional>
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::scifi {
struct GhostSpec {
    double      born;
    SimParticle particle;
};
/** @brief Own source periodic particle recipes; no frame, actor or caller references retained. */
struct Ghosts {
    double                               period, ground;
    std::optional<std::array<double, 4>> box;
    std::vector<GhostSpec>               specs;
    std::vector<uint32_t>                sparks{HOT.begin(), HOT.end()};
    uint32_t                             haze = 0;
    explicit Ghosts(double duration, double floor = INFINITY, std::optional<std::array<double, 4>> clip = {})
        : period(duration), ground(floor), box(clip) {}
    /** @brief Append an owning source emission recipe during preparation.
     * @cost Amortized allocation per authored emitter, reused by all frame evaluations. */
    void add(double born, ParticleKind kind, double x, double y, double vx, double vy, double life,
             uint32_t color = WHITE, double size = 1, ParticleFlag flags = ParticleFlag::None) {
        const uint32_t n    = uint32_t(specs.size());
        double         seed = rand({n, 41}) * 100;
        if (kind == ParticleKind::Star) {
            const double m = std::ceil((8 * period) / TAU), f = (TAU * m) / period - 8;
            if (f < 5) seed = f + 5 * std::floor(rand({n, 43}) * 19);
        }
        specs.push_back({wrap(born, period), {kind, x, y, vx, vy, life, life, size, seed, 0, color, flags}});
    }
    /** @brief Reconstruct one emitter at a supplied age, returning an owning state or explicit absence.
     * @cost Replays source1/240-second steps per age second; used only by explicit frame evaluation. */
    std::optional<SimParticle> state(const GhostSpec& s, double age) const {
        SimParticle q    = s.particle;
        int         hops = 0;
        double      a    = 0;
        while (a < age - 1e-9) {
            const double dt         = std::min(1. / 240, age - a);
            bool         integrated = false;
            switch (q.kind) {
                case ParticleKind::Fire:
                    q.vy -= 95 * dt;
                    q.vx += std::sin(a * 17 + q.seed) * 30 * dt;
                    q.vx -= q.vx * 5 * dt;
                    q.vy -= q.vy * 3.2 * dt;
                    break;
                case ParticleKind::Grit: {
                    q.vy += GRAVITY * dt;
                    q.vx -= q.vx * .8 * dt;
                    const double ny = q.y + q.vy * dt;
                    q.x += q.vx * dt;
                    if (q.vy > 0 && q.y <= ground && ny >= ground) {
                        q.y = ground - .25;
                        if (q.vy > 130 && rand({jsUint(q.seed * 1000), uint32_t(hops++)}) < .4) {
                            q.vy = -q.vy * .25;
                            q.vx = q.vx * .5;
                        } else {
                            q.vx   = 0;
                            q.vy   = 0;
                            q.life = std::min(q.life, .5);
                        }
                    } else
                        q.y = ny;
                    integrated = true;
                    break;
                }
                case ParticleKind::Spark: {
                    q.vy += GRAVITY * .45 * dt;
                    q.vx -= q.vx * 2.2 * dt;
                    q.vy -= q.vy * 2.2 * dt;
                    const double ny = q.y + q.vy * dt;
                    q.x += q.vx * dt;
                    if (q.vy > 0 && q.y <= ground && ny >= ground) {
                        q.vy = -q.vy * .45;
                        q.vx *= .7;
                        q.y = ground - .25;
                    } else
                        q.y = ny;
                    integrated = true;
                    break;
                }
                case ParticleKind::Streak:
                    q.vx += q.vx * 3 * dt;
                    q.vy += q.vy * 3 * dt;
                    break;
                case ParticleKind::Flake:
                    q.vx -= q.vx * 2.2 * dt;
                    q.vy -= q.vy * 2.2 * dt - 35 * dt;
                    q.x += std::sin(a * 2.5 + q.seed) * 7 * dt;
                    break;
                case ParticleKind::Ember:
                    q.vy += (-30 + std::sin(a * 3 + q.seed) * 20) * dt;
                    q.vx += std::cos(a * 2.3 + q.seed) * 45 * dt;
                    q.vx -= q.vx * 1.6 * dt;
                    q.vy -= q.vy * 1.6 * dt;
                    break;
                case ParticleKind::Smoke:
                    q.vy -= (hasFlag(q.flags, ParticleFlag::Big) ? 23 : 16) * dt;
                    q.vx += std::sin(a * 1.3 + q.seed) * 8 * dt;
                    q.vx -= q.vx * 1.6 * dt;
                    q.vy -= q.vy * 1.2 * dt;
                    break;
                case ParticleKind::Mote:
                    q.vx -= q.vx * .6 * dt;
                    q.vy -= q.vy * .6 * dt;
                    break;
                default: {
                    const double drag = isSoft(q.kind) ? 4 : 1.6;
                    q.vx -= q.vx * drag * dt;
                    q.vy -= q.vy * drag * dt + 5 * dt;
                    break;
                }
            }
            if (!integrated) {
                q.x += q.vx * dt;
                q.y += q.vy * dt;
            }
            a += dt;
            q.life -= dt;
            if (q.life <= 0) {
                if (q.kind != ParticleKind::Fire || !hasFlag(q.flags, ParticleFlag::Smokes)) return std::nullopt;
                const bool   big = hasFlag(q.flags, ParticleFlag::Big);
                const double L   = big ? 1.3 + rand({jsUint(s.particle.seed * 7), 1})
                                       : .8 + rand({jsUint(s.particle.seed * 7), 1}) * .6;
                q.kind           = ParticleKind::Smoke;
                q.vx             = q.vx * .6;
                q.vy             = q.vy * .6 - 6;
                q.life           = L;
                q.max            = L;
                q.color          = 0;
                q.size           = q.size * (big ? .85 : .7);
                q.flags          = big ? ParticleFlag::Big : ParticleFlag::None;
            }
        }
        if (box && (q.x < (*box)[0] || q.x > (*box)[2] || q.y < (*box)[1] || q.y > (*box)[3])) return std::nullopt;
        return q;
    }
    /** @brief Evaluate an owning particle frame at absolute periodic time without changing recipes.
     * @cost Reconstructs every live emitter/carry-over at source240Hz, allocating frame-local state. */
    Particles evaluate(double t) const {
        Particles ps;
        ps.sparks = sparks;
        ps.haze   = haze;
        ps.time   = t;
        for (const auto& s : specs) {
            const double span = s.particle.kind == ParticleKind::Fire && hasFlag(s.particle.flags, ParticleFlag::Smokes)
                                    ? s.particle.life + 2.4
                                    : s.particle.life;
            for (double age = wrap(t - s.born, period); age < span; age += period)
                if (auto q = state(s, age)) ps.list.push_back(*q);
        }
        return ps;
    }
};
/** @brief Return source positive event age in one period; no retained state. */
inline double since(double t, double event, double period) { return wrap(t - event, period); }
}  // namespace eve::stylize::pixelfx::scifi
