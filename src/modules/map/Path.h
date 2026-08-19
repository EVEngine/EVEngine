#pragma once

#include <vector>

namespace eve::map {

/** Ordered tile-index waypoints from start to goal (inclusive). */
class Path {
public:
    void clear();
    void add(int x, int y);
    void reverse();

    int getLength() const;
    int getX(int index) const;
    int getY(int index) const;
    float getTotalCost() const;
    void setTotalCost(float cost);

    bool empty() const { return cells_.empty(); }

    struct Cell {
        int x = 0;
        int y = 0;
    };

    const std::vector<Cell> &cells() const { return cells_; }

private:
    std::vector<Cell> cells_;
    float totalCost_ = 0.f;
};

}  // namespace eve::map
