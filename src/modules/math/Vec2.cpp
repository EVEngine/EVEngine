#include "math/Vec2.h"

#include "common/Exception.h"

#include <cmath>

namespace eve::math {

void Vec2::normalize() {
    float len = length();
    if (len > 0.f) {
        x_ /= len;
        y_ /= len;
    }
}

Vec2 *Vec2::normalized() const {
    auto *v = new Vec2(x_, y_);
    v->normalize();
    return v;
}

float Vec2::dot(const Vec2 *other) const {
    if (!other) throw eve::Exception("Vec2.dot: other is null");
    return x_ * other->x_ + y_ * other->y_;
}

float Vec2::cross(const Vec2 *other) const {
    if (!other) throw eve::Exception("Vec2.cross: other is null");
    return x_ * other->y_ - y_ * other->x_;
}

float Vec2::distanceTo(const Vec2 *other) const {
    if (!other) throw eve::Exception("Vec2.distanceTo: other is null");
    float dx = x_ - other->x_;
    float dy = y_ - other->y_;
    return std::sqrt(dx * dx + dy * dy);
}

float Vec2::angle() const { return std::atan2(y_, x_); }

Vec2 *Vec2::add(const Vec2 *other) const {
    if (!other) throw eve::Exception("Vec2.add: other is null");
    return new Vec2(x_ + other->x_, y_ + other->y_);
}

Vec2 *Vec2::sub(const Vec2 *other) const {
    if (!other) throw eve::Exception("Vec2.sub: other is null");
    return new Vec2(x_ - other->x_, y_ - other->y_);
}

Vec2 *Vec2::scale(float s) const { return new Vec2(x_ * s, y_ * s); }

Vec2 *Vec2::lerpTo(const Vec2 *other, float t) const {
    if (!other) throw eve::Exception("Vec2.lerpTo: other is null");
    return new Vec2(x_ + (other->x_ - x_) * t, y_ + (other->y_ - y_) * t);
}

Vec2 *Vec2::clone() const { return new Vec2(x_, y_); }

}  // namespace eve::math
