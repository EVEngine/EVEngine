#pragma once

#include <cmath>

namespace eve::math {

class Vec2 {
public:
    Vec2() = default;
    Vec2(float x, float y) : x_(x), y_(y) {}

    float getX() const { return x_; }
    float getY() const { return y_; }
    void  setX(float x) { x_ = x; }
    void  setY(float y) { y_ = y; }
    void  set(float x, float y) {
        x_ = x;
        y_ = y;
    }

    float length() const { return std::sqrt(x_ * x_ + y_ * y_); }
    float lengthSquared() const { return x_ * x_ + y_ * y_; }

    void  normalize();
    Vec2 *normalized() const;

    float  dot(const Vec2 *other) const;
    float  cross(const Vec2 *other) const;
    float  distanceTo(const Vec2 *other) const;
    float  angle() const;

    Vec2 *add(const Vec2 *other) const;
    Vec2 *sub(const Vec2 *other) const;
    Vec2 *scale(float s) const;
    Vec2 *lerpTo(const Vec2 *other, float t) const;
    Vec2 *clone() const;

private:
    float x_ = 0.f;
    float y_ = 0.f;
};

}  // namespace eve::math
