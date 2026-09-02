#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

namespace eve::graphics {

/** @brief Fill rule applied to closed 2D path contours. */
enum class PathFillRule : std::uint8_t { NonZero, EvenOdd };

/** @brief One flattened owning contour produced from a Path2D. */
struct FlattenedContour2D {
    std::vector<glm::vec2> points;
    bool                   closed = false;
};

/**
 * @brief Owning backend-neutral 2D vector path.
 *
 * Path mutation and flattening are CPU-only and thread-compatible when each
 * instance has one owner. The object retains no Canvas or backend pointers.
 */
class Path2D {
public:
    /** @brief Starts a new contour at point. */
    Path2D& moveTo(glm::vec2 point);
    /** @brief Adds a straight segment to point. */
    Path2D& lineTo(glm::vec2 point);
    /** @brief Adds a quadratic Bezier segment. */
    Path2D& quadTo(glm::vec2 control, glm::vec2 point);
    /** @brief Adds a cubic Bezier segment. */
    Path2D& cubicTo(glm::vec2 control1, glm::vec2 control2, glm::vec2 point);
    /** @brief Closes the current contour. */
    Path2D& close();
    /** @brief Removes all verbs and points while retaining capacity. */
    void clear() noexcept;

    /**
     * @brief Flattens curves into owning line contours.
     * @param tolerance Maximum control-point deviation in path units; positive and finite.
     * @param maxDepth Hard recursion bound used for adversarial curves.
     */
    [[nodiscard]] std::vector<FlattenedContour2D> flatten(float tolerance = 0.25f, std::uint32_t maxDepth = 12) const;

    void                       setFillRule(PathFillRule rule) noexcept { fillRule_ = rule; }
    [[nodiscard]] PathFillRule fillRule() const noexcept { return fillRule_; }
    [[nodiscard]] bool         empty() const noexcept { return verbs_.empty(); }

private:
    enum class Verb : std::uint8_t { Move, Line, Quad, Cubic, Close };

    std::vector<Verb>      verbs_;
    std::vector<glm::vec2> points_;
    PathFillRule           fillRule_    = PathFillRule::NonZero;
    bool                   contourOpen_ = false;
};

}  // namespace eve::graphics
