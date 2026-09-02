#include "graphics/PrimitivePath.h"

#include "common/Assert.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace eve::graphics {
namespace {

void validatePoint(glm::vec2 point) {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    EV_PARAM_CHECK(finite, "path points must be finite");
}

float pointLineDistance(glm::vec2 point, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 line          = b - a;
    const float     lengthSquared = glm::dot(line, line);
    if (lengthSquared <= 1e-20f) return glm::length(point - a);
    const float t = glm::clamp(glm::dot(point - a, line) / lengthSquared, 0.f, 1.f);
    return glm::length(point - (a + line * t));
}

void flattenQuad(glm::vec2 a, glm::vec2 control, glm::vec2 b, float tolerance, std::uint32_t depth,
                 std::uint32_t maxDepth, std::vector<glm::vec2>& output) {
    if (depth >= maxDepth || pointLineDistance(control, a, b) <= tolerance) {
        output.push_back(b);
        return;
    }
    const glm::vec2 ac     = (a + control) * 0.5f;
    const glm::vec2 cb     = (control + b) * 0.5f;
    const glm::vec2 middle = (ac + cb) * 0.5f;
    flattenQuad(a, ac, middle, tolerance, depth + 1, maxDepth, output);
    flattenQuad(middle, cb, b, tolerance, depth + 1, maxDepth, output);
}

void flattenCubic(glm::vec2 a, glm::vec2 c1, glm::vec2 c2, glm::vec2 b, float tolerance, std::uint32_t depth,
                  std::uint32_t maxDepth, std::vector<glm::vec2>& output) {
    const float deviation = std::max(pointLineDistance(c1, a, b), pointLineDistance(c2, a, b));
    if (depth >= maxDepth || deviation <= tolerance) {
        output.push_back(b);
        return;
    }
    const glm::vec2 a1           = (a + c1) * 0.5f;
    const glm::vec2 c12          = (c1 + c2) * 0.5f;
    const glm::vec2 c2b          = (c2 + b) * 0.5f;
    const glm::vec2 leftControl  = (a1 + c12) * 0.5f;
    const glm::vec2 rightControl = (c12 + c2b) * 0.5f;
    const glm::vec2 middle       = (leftControl + rightControl) * 0.5f;
    flattenCubic(a, a1, leftControl, middle, tolerance, depth + 1, maxDepth, output);
    flattenCubic(middle, rightControl, c2b, b, tolerance, depth + 1, maxDepth, output);
}

}  // namespace

Path2D& Path2D::moveTo(glm::vec2 point) {
    validatePoint(point);
    verbs_.push_back(Verb::Move);
    points_.push_back(point);
    contourOpen_ = true;
    return *this;
}

Path2D& Path2D::lineTo(glm::vec2 point) {
    EV_PARAM_CHECK(contourOpen_, "lineTo requires a preceding moveTo");
    validatePoint(point);
    verbs_.push_back(Verb::Line);
    points_.push_back(point);
    return *this;
}

Path2D& Path2D::quadTo(glm::vec2 control, glm::vec2 point) {
    EV_PARAM_CHECK(contourOpen_, "quadTo requires a preceding moveTo");
    validatePoint(control);
    validatePoint(point);
    verbs_.push_back(Verb::Quad);
    points_.push_back(control);
    points_.push_back(point);
    return *this;
}

Path2D& Path2D::cubicTo(glm::vec2 control1, glm::vec2 control2, glm::vec2 point) {
    EV_PARAM_CHECK(contourOpen_, "cubicTo requires a preceding moveTo");
    validatePoint(control1);
    validatePoint(control2);
    validatePoint(point);
    verbs_.push_back(Verb::Cubic);
    points_.push_back(control1);
    points_.push_back(control2);
    points_.push_back(point);
    return *this;
}

Path2D& Path2D::close() {
    EV_PARAM_CHECK(contourOpen_, "close requires an open contour");
    verbs_.push_back(Verb::Close);
    contourOpen_ = false;
    return *this;
}

void Path2D::clear() noexcept {
    verbs_.clear();
    points_.clear();
    contourOpen_ = false;
}

std::vector<FlattenedContour2D> Path2D::flatten(float tolerance, std::uint32_t maxDepth) const {
    const bool validTolerance = std::isfinite(tolerance) && tolerance > 0.f;
    EV_PARAM_CHECK(validTolerance, "path flatten tolerance must be finite and positive");
    const bool validMaxDepth = maxDepth > 0 && maxDepth <= 24;
    EV_PARAM_CHECK(validMaxDepth, "path flatten maxDepth must be in [1, 24]");

    std::vector<FlattenedContour2D> result;
    std::size_t                     pointIndex = 0;
    glm::vec2                       current{0.f};
    for (Verb verb : verbs_) {
        switch (verb) {
            case Verb::Move:
                result.push_back({});
                current = points_[pointIndex++];
                result.back().points.push_back(current);
                break;
            case Verb::Line:
                current = points_[pointIndex++];
                result.back().points.push_back(current);
                break;
            case Verb::Quad: {
                const glm::vec2 control = points_[pointIndex++];
                const glm::vec2 end     = points_[pointIndex++];
                flattenQuad(current, control, end, tolerance, 0, maxDepth, result.back().points);
                current = end;
                break;
            }
            case Verb::Cubic: {
                const glm::vec2 control1 = points_[pointIndex++];
                const glm::vec2 control2 = points_[pointIndex++];
                const glm::vec2 end      = points_[pointIndex++];
                flattenCubic(current, control1, control2, end, tolerance, 0, maxDepth, result.back().points);
                current = end;
                break;
            }
            case Verb::Close: result.back().closed = true; break;
        }
    }
    return result;
}

}  // namespace eve::graphics
