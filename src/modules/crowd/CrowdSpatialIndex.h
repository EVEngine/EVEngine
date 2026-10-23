#pragma once
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include "common/Export.h"

namespace eve::crowd {
/** @brief Non-allocating internal unwind signal, translated at the checked simulation boundary. */
struct WorkLimit {
    const char* message;
};
/** @brief Canonical in-process tie ordering; named identities sort before anonymous admission ordinals. */
struct CrowdTieKey {
    bool          anonymous                                           = true;
    std::uint64_t value                                               = 0;
    friend auto   operator<=>(const CrowdTieKey&, const CrowdTieKey&) = default;
};

/** @brief Owning sparse broadphase workspace; integer cell/row order and stable tie-key bucket order.
 * @ownership Contains numeric slots only, no borrowed positions or entity pointers.
 * @thread Owner thread. @reentrancy Queries cannot mutate the index or call unknown callbacks.
 * @cost Storage scales with agents and occupied cells, not their bounding rectangle.
 */
class EVENGINE_API_FOUNDATION CrowdSpatialIndex {
    struct Member {
        std::int64_t x, y;
        CrowdTieKey  tie;
        int          slot;
    };
    struct Cell {
        std::int64_t x, y;
        std::size_t  begin, end;
    };
    struct Row {
        std::int64_t y;
        std::size_t  begin, end;
    };
    std::vector<Member> members_;
    std::vector<Cell>   cells_;
    std::vector<Row>    rows_;
    double              cellSize_ = 1.0;
    static std::int64_t coordinate(double value, double cell);
    static std::int64_t queryBound(double value, double cell) noexcept;

public:
    /** @brief Rebuild owning directories; failure is handled by the surrounding advance rollback.
     * @ownership Copies numeric values; input spans are borrowed only for this call.
     * @thread Owner thread. @reentrancy No callbacks.
     * @cost Agent sorting and occupied-cell grouping per rebuild, amortized by queries in that phase.
     */
    void rebuild(std::span<const float> xs, std::span<const float> ys, std::span<const CrowdTieKey> ties,
                 double cellSize, std::int64_t maxCells, std::int64_t& comparisons);
    /** @brief Invalidate derived directories without releasing reusable capacity; no callbacks. */
    void clear() noexcept {
        members_.clear();
        cells_.clear();
        rows_.clear();
    }
    /** @brief Observe occupied cell count; constant time, owner thread. */
    [[nodiscard]] std::size_t cellCount() const noexcept { return cells_.size(); }
    /** @brief Enumerate conservative candidates in a query box with explicit lookup-work charging.
     * @ownership Borrows this index during the call. Callback must not mutate it or retain index references.
     * @thread Owner thread. @reentrancy Only known synchronous internal callbacks.
     * @cost Row/cell binary searches plus occupied cells and candidates in the requested box.
     */
    template <class Fn>
    void visit(double x, double y, double radius, std::int64_t maxVisits, std::int64_t& visits,
               std::int64_t& visitedCells, Fn&& fn) const {
        if (!std::isfinite(radius)) throw WorkLimit{"Crowd neighborhood radius is nonfinite"};
        coordinate(x, cellSize_);
        coordinate(y, cellSize_);
        if (radius <= 0.0 || rows_.empty()) return;
        const auto x0 = queryBound(x - radius, cellSize_), x1 = queryBound(x + radius, cellSize_);
        const auto y0 = queryBound(y - radius, cellSize_), y1 = queryBound(y + radius, cellSize_);
        const auto spend = [&] {
            if (visits >= maxVisits) throw WorkLimit{"Crowd spatial lookup budget exhausted"};
            ++visits;
        };
        auto row = std::lower_bound(rows_.begin(), rows_.end(), y0, [&](const Row& candidate, auto key) {
            spend();
            return candidate.y < key;
        });
        for (; row != rows_.end() && row->y <= y1; ++row) {
            spend();
            const auto end = cells_.begin() + row->end;
            auto cell = std::lower_bound(cells_.begin() + row->begin, end, x0, [&](const Cell& candidate, auto key) {
                spend();
                return candidate.x < key;
            });
            for (; cell != end && cell->x <= x1; ++cell) {
                spend();
                ++visitedCells;
                for (std::size_t i = cell->begin; i < cell->end; ++i) fn(members_[i].slot);
            }
        }
    }

};
}  // namespace eve::crowd
