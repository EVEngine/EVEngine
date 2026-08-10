#include "map/FlowField.h"

namespace eve::map {

void FlowField::clear() {
    width_ = height_ = 0;
    goalX_ = goalY_ = 0;
    cost_.clear();
    nextX_.clear();
    nextY_.clear();
}

void FlowField::resize(int width, int height) {
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    const int n = width_ * height_;
    cost_.assign(size_t(n), kUnreachable);
    nextX_.assign(size_t(n), 0);
    nextY_.assign(size_t(n), 0);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int i = index(x, y);
            nextX_[size_t(i)] = x;
            nextY_[size_t(i)] = y;
        }
    }
}

void FlowField::setGoal(int x, int y) {
    goalX_ = x;
    goalY_ = y;
}

bool FlowField::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

float FlowField::costAt(int x, int y) const {
    if (!inBounds(x, y)) return kUnreachable;
    return cost_[size_t(index(x, y))];
}

void FlowField::setCost(int x, int y, float cost) {
    if (!inBounds(x, y)) return;
    cost_[size_t(index(x, y))] = cost;
}

int FlowField::nextX(int x, int y) const {
    if (!inBounds(x, y)) return x;
    return nextX_[size_t(index(x, y))];
}

int FlowField::nextY(int x, int y) const {
    if (!inBounds(x, y)) return y;
    return nextY_[size_t(index(x, y))];
}

void FlowField::setNext(int x, int y, int nx, int ny) {
    if (!inBounds(x, y)) return;
    const int i = index(x, y);
    nextX_[size_t(i)] = nx;
    nextY_[size_t(i)] = ny;
}

bool FlowField::isReachable(int x, int y) const {
    const float c = costAt(x, y);
    return c < kUnreachable && c >= 0.f;
}

}  // namespace eve::map
