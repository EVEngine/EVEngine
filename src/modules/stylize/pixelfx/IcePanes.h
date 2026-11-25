// Native adaptation of pixelfx ice/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "common/Export.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::ice {
/** @brief Independent polygon centroid and unsigned area; no references or callbacks. */
struct Centroid {
    double x, y, area;
};
/** @brief Evaluate trusted nonempty polygon geometry, using the source mean for degenerate area.
 * @param pts Synchronously borrowed corners, never retained.
 * @return Independent center and unsigned area; linear work without allocation. */
[[nodiscard]] EVENGINE_API_WORLD Centroid centroid(std::span<const Point> pts);

/** @brief Test a trusted borrowed polygon synchronously with the source even-odd rule. */
EVENGINE_API_WORLD bool isInsidePoly(std::span<const Point> pts, double x, double y);
/** @brief Split a trusted polygon, returning independently owned left/right pieces with at least three corners.
 * @cost Allocates at most two polygons per explicit cut, linear in input corners.
 * @note Caller-thread affine; no references or callbacks survive. */
EVENGINE_API_WORLD std::vector<std::vector<Point>> splitPoly(std::span<const Point> pts, double x, double y,
                                                             double angle);
/** @brief Blend a trusted glass polygon using source local-cell traversal; borrows end with this call.
 * @cost Tests the polygon bounding area per draw; no allocation or retained callbacks. */
EVENGINE_API_WORLD void glassPoly(Paint& p, std::span<const Point> pts, uint32_t color, double alpha);
/** @brief Owning authored ice fragment; sole mutable owner is its Panes instance, caller-thread affine. */
struct Pane {
    std::vector<Point> pts;
    double x = 0, y = 0, vx = 0, vy = 0, a = 0, va = 0, f = 0, vf = 0, lit = .6, g = 1, drag = .3, bounce = .35,
           life = 1, max = 1;
    bool   rest = false, solid = false;
    int    brittle = 0, hits = 0;
    std::optional<double> crumble, tone;
    double                restT = 0, ridge = -PI / 2;
};
/** @brief Own all mutable panes; no retained RNG, actor, callback or target references. */
struct Panes {
    std::vector<Pane> list;
    /** @brief Transfer an authored pane into this owner; input storage is not borrowed.
     * @cost Amortized constant append per fragment; owned storage may grow at emission. */
    void add(Pane q) {
        q.max = q.life;
        list.push_back(std::move(q));
    }
    /** @brief Return an independent projected-corner snapshot; retains no input references.
     * @cost Allocates one point per authored corner per explicit snapshot/draw. */
    EVENGINE_API_WORLD std::vector<Point> corners(const Pane& q) const;
    /** @brief Evaluate the lowest projected corner synchronously, without allocation. */
    EVENGINE_API_WORLD double seat(const Pane& q) const;
    /** @brief Draw owned panes into a synchronous target/ramp borrow, without advancing time.
     * @cost Projects corners and rasterizes each visible fragment's bounding area per draw. */
    EVENGINE_API_WORLD void draw(Paint& p, std::span<const uint32_t> C) const;
    /** @brief Advance by an injected fixed tick using trusted compiled callbacks borrowed only during this call.
     * @param dt Positive fixed source tick.
     * @param ground Evaluate floor height at x; never retained.
     * @param onLand Observe a hard landing before brittle removal or rebound.
     * @param onCrumble Observe a resting fragment immediately before removal.
     * @param onSkid Observe sliding before movement; source floor coordinate is supplied.
     * @note Caller-thread affine, no locks. Callbacks may emit into other owning systems;
     * they must not mutate this list or retain Pane references. Reverse erase preserves source order. */
    template <class Ground, class Land, class Crumble, class Skid>
    void update(double dt, Ground ground, Land onLand, Crumble onCrumble, Skid onSkid) {
        for (size_t left = list.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        q = list[i];
            q.life -= dt;
            if (q.life <= 0) {
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            const double floor = ground(q.x) - .5;
            if (q.rest && q.crumble && (q.restT += dt) > *q.crumble) {
                onCrumble(q);
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (q.rest) {
                if (std::abs(q.vx) > 8) onSkid(q, q.x, floor + .5);
                q.x += q.vx * dt;
                q.vx -= q.vx * 7 * dt;
                q.a += q.va * dt;
                q.va -= q.va * 9 * dt;
                q.vf -= q.vf * 9 * dt;
                q.f += q.vf * dt;
                q.y -= seat(q) - floor;
                continue;
            }
            q.vy += GRAVITY * q.g * dt;
            q.vx -= q.vx * q.drag * dt;
            q.vy -= q.vy * q.drag * dt;
            q.x += q.vx * dt;
            q.y += q.vy * dt;
            q.a += q.va * dt;
            q.f += q.vf * dt;
            const double low = seat(q);
            if (q.g <= 0 || low < floor || q.vy <= 0) continue;
            q.y -= low - floor;
            if (q.vy > 70) {
                ++q.hits;
                onLand(q, q.x, floor + .5, q.vy);
            }
            if (q.brittle && q.vy > 70 && q.hits >= q.brittle) {
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (q.vy > 70 && q.bounce > 0) {
                q.vy = -q.vy * q.bounce;
                q.vx *= .75;
                q.va *= .6;
                q.vf *= .5;
            } else {
                q.rest = true;
                q.vy   = 0;
            }
        }
    }
};
}  // namespace eve::stylize::pixelfx::ice
