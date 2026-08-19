#pragma once

#include <box3d/id.h>

namespace eve::physics {

class Body3D;
class World3D;

/**
 * @brief 3D shape (box/sphere/capsule) attached to a Body3D with material
 * settings. Created via Body3D::new*Shape; owned by the world.
 */
class Shape3D {
public:
    /** @brief Shape geometry kind. */
    enum class Kind { Box, Sphere, Capsule };

    /** @brief Internal: wraps a Box3D shape (use Body3D::new*Shape). */
    Shape3D(World3D *world, Body3D *body, b3ShapeId shapeId, Kind kind, float a, float b,
            float c);
    ~Shape3D();

    Shape3D(const Shape3D &)            = delete;
    Shape3D &operator=(const Shape3D &) = delete;

    /** @brief Sensor shapes report contacts but never collide. */
    void setSensor(bool sensor);
    bool isSensor() const;

    /** @brief Material properties. */
    void  setFriction(float friction);
    float getFriction() const;

    void  setRestitution(float restitution);
    float getRestitution() const;

    void  setDensity(float density);
    float getDensity() const;

    /** @brief Owning body. */
    Body3D *getBody() { return body_; }

    /** @brief Point-in-shape test in world meters. */
    bool testPoint(float x, float y, float z) const;

    /** @brief Destroys the shape inside its world. */
    void destroy();

    /** @brief Raw Box3D shape id / liveness. */
    b3ShapeId raw() const { return shapeId_; }
    bool      isValid() const;

    /** @brief Internal: marks the wrapper invalid after destruction. */
    void invalidate();

private:
    friend class World3D;
    friend class Body3D;

    void recreate(bool sensor);

    World3D  *world_ = nullptr;
    Body3D   *body_  = nullptr;
    b3ShapeId shapeId_{};
    Kind      kind_ = Kind::Box;
    float     a_ = 0.f;  // box hx | sphere r | capsule half-height
    float     b_ = 0.f;  // box hy | unused   | capsule radius
    float     c_ = 0.f;  // box hz
};

}  // namespace eve::physics
