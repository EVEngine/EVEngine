#pragma once

#include <cmath>

namespace eve::math {

class Vec3 {
public:
    Vec3() = default;
    Vec3(float x, float y, float z) : x_(x), y_(y), z_(z) {}

    float getX() const { return x_; }
    float getY() const { return y_; }
    float getZ() const { return z_; }
    void  setX(float x) { x_ = x; }
    void  setY(float y) { y_ = y; }
    void  setZ(float z) { z_ = z; }
    void  set(float x, float y, float z) {
        x_ = x;
        y_ = y;
        z_ = z;
    }

    float length() const { return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_); }
    float lengthSquared() const { return x_ * x_ + y_ * y_ + z_ * z_; }

    void  normalize();
    Vec3 *normalized() const;

    float  dot(const Vec3 *other) const;
    Vec3 *cross(const Vec3 *other) const;
    float  distanceTo(const Vec3 *other) const;

    Vec3 *add(const Vec3 *other) const;
    Vec3 *sub(const Vec3 *other) const;
    Vec3 *scale(float s) const;
    Vec3 *lerpTo(const Vec3 *other, float t) const;
    Vec3 *clone() const;

private:
    float x_ = 0.f;
    float y_ = 0.f;
    float z_ = 0.f;
};

}  // namespace eve::math
