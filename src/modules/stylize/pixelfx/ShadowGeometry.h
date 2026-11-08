// eve-hot-path
// Native adaptation of pixelfx shadow/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Shadow.h"
namespace eve::stylize::pixelfx::shadow {
/** @brief Test the source four-point star with concave sides; all coordinates are values. */
inline bool isInsideStar(double x, double y, double cx, double cy, double length, double rotation, double q = .42) {
    if (length <= 0) return false;
    const double c = std::cos(rotation), s = std::sin(rotation), dx = x - cx, dy = y - cy;
    const double u = std::abs(dx * c + dy * s) / length, v = std::abs(-dx * s + dy * c) / length;
    return u < 1 && v < 1 && std::min(u, v) <= std::pow(.5, 1 / q) && std::pow(u, q) + std::pow(v, q) <= 1;
}
/** @brief Own one rasterized source black-star geometry snapshot, independent of painters and effect state.
 * @note Caller-thread use; compiled Dark Explosion inputs bound length to0..60. No external pointers or callbacks.
 * @cost Allocates and rasterizes O(length squared) cells once per explicit construction, reused by draw/unlight. */
class BlackStar {
    double               cy_;
    int                  radius_ = 0, size_ = 0;
    std::vector<uint8_t> cells_;

public:
    /** @brief Prepare source arms and their one-cell spines from trusted finite compiled inputs. */
    BlackStar(double cy, double length, double rotation, double shortArm = .46, double q = .32) : cy_(cy) {
        EV_ASSERT((std::isfinite(length) && length >= 0 && length <= 60));
        radius_ = int(std::ceil(length)) + 2;
        size_   = radius_ * 2 + 1;
        cells_.resize(size_t(size_) * size_t(size_));
        for (int j = 0; j < size_; ++j)
            for (int i = 0; i < size_; ++i) {
                const double x = i - radius_ + .5, y = cy_ + j - radius_ + .5;
                if (isInsideStar(x, y, 0, cy_, length, rotation, q) ||
                    isInsideStar(x, y, 0, cy_, length * shortArm, rotation + PI / 4, q))
                    cells_[size_t(j * size_ + i)] = 1;
            }
        for (int a = 0; a < 4; ++a) {
            const double ca = std::cos(rotation + a * PI / 2), sa = std::sin(rotation + a * PI / 2);
            for (double d = 0; d < length - 1; d += .5) {
                const int i = int(std::floor(ca * d)) + radius_, j = int(std::floor(sa * d)) + radius_;
                cells_[size_t(j * size_ + i)] = 1;
            }
        }
    }
    /** @brief Query a local source cell in the owning raster without mutation. */
    bool isDarkCell(double x, double y) const {
        const int i = int(std::floor(x)) + radius_, j = int(std::floor(y - cy_)) + radius_;
        return i >= 0 && j >= 0 && i < size_ && j < size_ && cells_[size_t(j * size_ + i)] == 1;
    }
    /** @brief Paint source black cells and a one-cell rim; skip and painter are synchronous borrows.
     * @cost Visits the prepared square once per draw without allocating. */
    template <class Skip>
    void draw(Paint& p, uint32_t color, uint32_t rim, Skip skip) const {
        for (int j = 0; j < size_; ++j)
            for (int i = 0; i < size_; ++i) {
                const double x = i - radius_ + .5, y = cy_ + j - radius_ + .5;
                if (cells_[size_t(j * size_ + i)])
                    p.px(x, y, color);
                else if (rim && !skip(x, y) &&
                         (isDarkCell(x - 1, y) || isDarkCell(x + 1, y) || isDarkCell(x, y - 1) || isDarkCell(x, y + 1)))
                    p.px(x, y, rim);
            }
    }
    /** @brief Clear light in the transformed source raster cells after all glows; painter is a synchronous borrow.
     * @note Obeys the destination clip extension; source with no clip remains identical.
     * @cost Visits prepared cells once per draw without allocating or retaining painter state. */
    void unlight(Paint& p) const {
        for (int j = 0; j < size_; ++j)
            for (int i = 0; i < size_; ++i) {
                if (!cells_[size_t(j * size_ + i)]) continue;
                const double x = i - radius_ + .5, y = cy_ + j - radius_ + .5;
                const int    sx = p.cellX(x, y), sy = p.cellY(x, y);
                if (p.contains(sx, sy) && p.isInsideClip(sx, sy)) p.lights[p.index(sx, sy)] = 0;
            }
    }
};
/** @brief Paint one source obsidian shard under a point light, borrowing debris, palette and painter synchronously.
 * @note Debris supplies source x/y/angle/size/life fields; palette has eight resolved source steps.
 * @cost Visits the shard bounding box once per draw and allocates no retained geometry. */
template <class Body>
void shard(Paint& p, const Body& b, std::span<const uint32_t> C, double lx, double ly, double lk) {
    const double   fa = std::min(1., b.life / .4), ca = std::cos(b.angle), sa = std::sin(b.angle),
                   L = std::max(2., b.size), W = std::max(2., L * .55);
    const double   side  = -sa * (lx - b.x) + ca * (ly - b.y) > 0 ? 1 : -1;
    const size_t   index = lk > .6 ? 6 : lk > .3 ? 5 : lk > .08 ? 4 : 3;
    const uint32_t rim   = C[index];
    // Source indexOf selects the first matching color, including repeated/resampled palette entries.
    const size_t first = size_t(std::find(C.begin(), C.end(), rim) - C.begin());
    const double E     = L / 2 + 1.5;
    p.shader(b.x - E, b.y - E, b.x + E, b.y + E, [&](double x, double y, ShadeOutput& out) {
        const double dx = x - b.x, dy = y - b.y, u = dx * ca + dy * sa, v = (-dx * sa + dy * ca) * side;
        if (u < -L / 2 - .4 || u > L / 2 + .4) return;
        const double half = ((W / 2) * (L / 2 - u)) / L + .4;
        if (std::abs(v) > half) return;
        out.a = fa;
        if (u > L / 2 - 1.3 && lk > .3) {
            out.c = WHITE;
            return;
        }
        if (v > half - 1) {
            out.c = u > -L / 4 ? rim : C[std::max(size_t(3), first == 0 ? size_t(0) : first - 1)];
            return;
        }
        out.c = v < -half + 1 ? C[1] : C[2];
    });
    if (lk > .04) p.glow(b.x, b.y, 3 + L * .6, C[5], .2 * lk * fa);
}
}  // namespace eve::stylize::pixelfx::shadow
