#pragma once
#include "common/Export.h"


#include <limits>
#include <vector>

namespace eve::map {

/**
 * @brief Integration + direction field for group pathfinding to a single goal.
 * nextX/nextY point to the neighboring cell with lower cost (toward goal).
 * At the goal, next points to itself.
 */
class EVENGINE_API_WORLD FlowField {
public:
    static constexpr float kUnreachable = std::numeric_limits<float>::infinity();

    /** @brief Clears . */
    void clear();
    /** @brief Resize. */
    void resize(int width, int height);
    /** @brief Returns the width. */
    int getWidth() const { return width_; }
    /** @brief Returns the height. */
    int getHeight() const { return height_; }

    /** @brief Returns the goal x. */
    int getGoalX() const { return goalX_; }
    /** @brief Returns the goal y. */
    int getGoalY() const { return goalY_; }
    /** @brief Sets the goal. */
    void setGoal(int x, int y);

    /** @brief Cost at. */
    float costAt(int x, int y) const;
    /** @brief Sets the cost. */
    void setCost(int x, int y, float cost);

    /** @brief Next x. */
    int nextX(int x, int y) const;
    /** @brief Next y. */
    int nextY(int x, int y) const;
    /** @brief Sets the next. */
    void setNext(int x, int y, int nx, int ny);

    /** @brief True when reachable. */
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
