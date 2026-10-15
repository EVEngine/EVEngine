#pragma once
#include <box3d/id.h>
#include <memory>
#include "common/Export.h"
#include "physics/backend/BodyBackend3D.h"
namespace eve::physics {
class Body3D;
class Shape3D;
class Joint3D;
class World3D;
namespace box3d {
/** @brief Internal native ABI adoption boundary; transfers sole body ownership, simulation thread only. */
[[nodiscard]] std::unique_ptr<IBodyBackend3D> adoptBody(b3BodyId id);
/** @brief Private bridge used exclusively by Box3D composition and native query code. */
struct EVENGINE_API_WORLD Access {
    /** @brief Borrows the native identifier synchronously; facade must belong to a Box3D world. */
    static b3BodyId bodyId(const Body3D& body);
    /** @brief Borrows a Box3D shape identifier for a synchronous native operation. */
    static b3ShapeId shapeId(const Shape3D& shape);
    /** @brief Internal checked Box3D joint ID; fails for another backend. */
    static b3JointId jointId(const Joint3D& joint);
    /** @brief Internal checked Box3D world ID; fails for another backend. */
    static b3WorldId worldId(const World3D& world);
};
}  // namespace box3d
}  // namespace eve::physics
