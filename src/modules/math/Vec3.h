#pragma once

#include <cmath>

namespace eve::math {

/** @brief 3D float vector (script-facing math module value). */
class Vec3 {
public:
    Vec3() = default;
    /** @brief Creates a vector from components. */
    Vec3(float x, float y, float z) : x_(x), y_(y), z_(z) {}

    /** @brief Component accessors. */
    float getX() const { return x_; }
    float getY() const { return y_; }
    float getZ() const { return z_; }
    void  setX(float x) { x_ = x; }
    void  setY(float y) { y_ = y; }
    void  setZ(float z) { z_ = z; }
    /** @brief Sets all three components. */
    void  set(float x, float y, float z) {
        x_ = x;
        y_ = y;
        z_ = z;
    }

    /** @brief Magnitude (and squared magnitude). */
    float length() const { return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_); }
    float lengthSquared() const { return x_ * x_ + y_ * y_ + z_ * z_; }

    /** @brief Normalizes in place / returns a normalized copy. */
    void  normalize();
    Vec3 *normalized() const;

    /** @brief Dot/cross product and distance. */
    float  dot(const Vec3 *other) const;
    Vec3 *cross(const Vec3 *other) const;
    float  distanceTo(const Vec3 *other) const;

    /** @brief Arithmetic helpers returning new (caller-owned) vectors. */
    Vec3 *add(const Vec3 *other) const;
    Vec3 *sub(const Vec3 *other) const;
    Vec3 *scale(float s) const;
    /** @brief Linear interpolation to `other` at t in [0,1]. */
    Vec3 *lerpTo(const Vec3 *other, float t) const;
    /** @brief Copies this vector. */
    Vec3 *clone() const;

private:
    float x_ = 0.f;
    float y_ = 0.f;
    float z_ = 0.f;
};

}  // namespace eve::math
