#pragma once

#include <box3d/id.h>

namespace eve::physics {

class Body3D;
class World3D;

class Shape3D {
public:
    enum class Kind { Box, Sphere, Capsule };

    Shape3D(World3D *world, Body3D *body, b3ShapeId shapeId, Kind kind, float a, float b,
            float c);
    ~Shape3D();

    Shape3D(const Shape3D &)            = delete;
    Shape3D &operator=(const Shape3D &) = delete;

    void setSensor(bool sensor);
    bool isSensor() const;

    void  setFriction(float friction);
    float getFriction() const;

    void  setRestitution(float restitution);
    float getRestitution() const;

    void  setDensity(float density);
    float getDensity() const;

    Body3D *getBody() { return body_; }

    /** Point-in-shape test in world meters. */
    bool testPoint(float x, float y, float z) const;

    void destroy();

    b3ShapeId raw() const { return shapeId_; }
    bool      isValid() const;

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
