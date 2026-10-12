#include "graphics/RenderTransformProjection.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics::detail {
glm::mat4 modelFromTransform(const Renderable3D::Transform3D& t) {
    if (t.hasLinkedWorld) return t.linkedWorld;
    glm::mat4 m(1.f);
    m = glm::translate(m, glm::vec3(t.x, t.y, t.z));
    m = glm::rotate(m, t.yaw, glm::vec3(0.f, 1.f, 0.f));
    m = glm::rotate(m, t.pitch, glm::vec3(1.f, 0.f, 0.f));
    m = glm::rotate(m, t.roll, glm::vec3(0.f, 0.f, 1.f));
    return glm::scale(m, glm::vec3(t.sx, t.sy, t.sz));
}
float sphereStretch(const Renderable3D::Transform3D& t) {
    if (!t.hasLinkedWorld) return std::max(std::abs(t.sx), std::max(std::abs(t.sy), std::abs(t.sz)));
    double sum = 0;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row) sum += double(t.linkedWorld[column][row]) * t.linkedWorld[column][row];
    return static_cast<float>(std::sqrt(sum));
}
}  // namespace eve::graphics::detail
