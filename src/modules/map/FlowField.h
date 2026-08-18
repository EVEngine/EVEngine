#pragma once

#include <limits>
#include <vector>

namespace eve::map {

/**
 * @brief Integration + direction field for group pathfinding to a single goal.
 * nextX/nextY point to the neighboring cell with lower cost (toward goal).
 * At the goal, next points to itself.
 */
class FlowField {
public:
    static constexpr float kUnreachable = std::numeric_limits<float>::infinity();

    void clear();
    void resize(int width, int height);
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    int getGoalX() const { return goalX_; }
    int getGoalY() const { return goalY_; }
    void setGoal(int x, int y);

    float costAt(int x, int y) const;
    void setCost(int x, int y, float cost);

    int nextX(int x, int y) const;
    int nextY(int x, int y) const;
    void setNext(int x, int y, int nx, int ny);

    bool isReachable(int x, int y) const;

private:
    bool inBounds(int x, int y) const;
    int index(int x, int y) const { return y * width_ + x; }

    int width_ = 0;
    int height_ = 0;
    int goalX_ = 0;
    int goalY_ = 0;
    std::vector<float> cost_;
    std::vector<int> nextX_;
    std::vector<int> nextY_;
};

}  // namespace eve::map
