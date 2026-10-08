#pragma once
#include "common/Export.h"


#include <cmath>

namespace eve::math {

/** @brief 2D float vector (script-facing math module value). */
class EVENGINE_API_FOUNDATION Vec2 {
public:
    Vec2() = default;
    /** @brief Creates a vector from components. */
    Vec2(float x, float y) : x_(x), y_(y) {}

    /** @brief Component accessors. */
    float getX() const { return x_; }
    /** @brief Y component. */
    float getY() const { return y_; }
    /** @brief Sets the X component. */
    void  setX(float x) { x_ = x; }
    /** @brief Sets the Y component. */
    void  setY(float y) { y_ = y; }
    /** @brief Sets both components. */
    void  set(float x, float y) {
        x_ = x;
        y_ = y;
    }

    /** @brief Magnitude (and squared magnitude). */
    float length() const { return std::sqrt(x_ * x_ + y_ * y_); }
    /** @brief Squared Euclidean magnitude. */
    float lengthSquared() const { return x_ * x_ + y_ * y_; }

    /** @brief Normalizes in place / returns a normalized copy. */
    void  normalize();
    Vec2 *normalized() const;

    /** @brief Atan2 angle in radians. */
    /** @brief Dot/cross product, distance, angle (radians). */
    float  dot(const Vec2 *other) const;
    /** @brief Cross product with other. */
    float  cross(const Vec2 *other) const;
    /** @brief Euclidean distance to other. */
    float  distanceTo(const Vec2 *other) const;
    /** @brief Atan2 angle in radians. */
    float  angle() const;

    /** @brief Arithmetic helpers returning new (caller-owned) vectors. */
    Vec2 *add(const Vec2 *other) const;
    Vec2 *sub(const Vec2 *other) const;
    Vec2 *scale(float s) const;
    /** @brief Linear interpolation to `other` at t in [0,1]. */
    Vec2 *lerpTo(const Vec2 *other, float t) const;
    /** @brief Copies this vector. */
    Vec2 *clone() const;

private:
    float x_ = 0.f;
    float y_ = 0.f;
};

}  // namespace eve::math
