#pragma once
#include <glm/mat4x4.hpp>

namespace eve::scene {
struct SceneNode;
namespace detail {
// Sole local TRS projection, shared by detached staging and live propagation.
[[nodiscard]] glm::mat4 localMatrix(const SceneNode& node);
}  // namespace detail
}  // namespace eve::scene
