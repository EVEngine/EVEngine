#pragma once
#include <box3d/id.h>
#include "physics/backend/ShapeBackend3D.h"
namespace eve::physics::box3d {
/** @brief Internal native shape factory; transfers native/cooked ownership on success.
 * @cost Cooks the supplied geometry once at creation, on the simulation thread.
 */
[[nodiscard]] eve::Result<std::unique_ptr<IShapeBackend3D>> makeShape(b3BodyId body, const ShapeGeometry3D& geometry,
                                                                      float density, float friction, float restitution);
}  // namespace eve::physics::box3d
