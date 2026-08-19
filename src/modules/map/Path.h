#pragma once

#include <vector>

namespace eve::map {

/** @brief Ordered tile-index waypoints from start to goal (inclusive). */
class Path {
public:
    /** @brief Clears all waypoints and cost. */
    void clear();
    /** @brief Appends a waypoint tile. */
    void add(int x, int y);
    /** @brief Reverses the waypoint order. */
    void reverse();

    /** @brief Number of waypoints (0 = empty/unreachable). */
    int getLength() const;
    /** @brief Waypoint tile coordinates. */
    int getX(int index) const;
    int getY(int index) const;
    /** @brief Total path cost (A* heuristic + movement cost). */
    float getTotalCost() const;
    void setTotalCost(float cost);

    /** @brief True when there are no waypoints. */
    bool empty() const { return cells_.empty(); }

    /** @brief One waypoint. */
    struct Cell {
        int x = 0;
        int y = 0;
    };

    /** @brief Raw waypoint vector. */
    const std::vector<Cell> &cells() const { return cells_; }

private:
    std::vector<Cell> cells_;
    float totalCost_ = 0.f;
};

}  // namespace eve::map
