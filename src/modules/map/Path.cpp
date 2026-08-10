#include "map/Path.h"

namespace eve::map {

void Path::clear() {
    cells_.clear();
    totalCost_ = 0.f;
}

void Path::add(int x, int y) { cells_.push_back(Cell{x, y}); }

void Path::reverse() {
    for (size_t i = 0, j = cells_.size(); i + 1 < j; ++i, --j) std::swap(cells_[i], cells_[j - 1]);
}

int Path::getLength() const { return int(cells_.size()); }

int Path::getX(int index) const {
    if (index < 0 || index >= int(cells_.size())) return 0;
    return cells_[size_t(index)].x;
}

int Path::getY(int index) const {
    if (index < 0 || index >= int(cells_.size())) return 0;
    return cells_[size_t(index)].y;
}

float Path::getTotalCost() const { return totalCost_; }

void Path::setTotalCost(float cost) { totalCost_ = cost; }

}  // namespace eve::map
