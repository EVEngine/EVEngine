#include "math/Vec3.h"

#include "common/Exception.h"

#include <cmath>

namespace eve::math {

void Vec3::normalize() {
    float len = length();
    if (len > 0.f) {
        x_ /= len;
        y_ /= len;
        z_ /= len;
    }
}

Vec3 *Vec3::normalized() const {
    auto *v = new Vec3(x_, y_, z_);
    v->normalize();
    return v;
}

float Vec3::dot(const Vec3 *other) const {
    if (!other) throw eve::Exception("Vec3.dot: other is null");
    return x_ * other->x_ + y_ * other->y_ + z_ * other->z_;
}

Vec3 *Vec3::cross(const Vec3 *other) const {
    if (!other) throw eve::Exception("Vec3.cross: other is null");
    return new Vec3(y_ * other->z_ - z_ * other->y_, z_ * other->x_ - x_ * other->z_,
                    x_ * other->y_ - y_ * other->x_);
}

float Vec3::distanceTo(const Vec3 *other) const {
    if (!other) throw eve::Exception("Vec3.distanceTo: other is null");
    float dx = x_ - other->x_;
    float dy = y_ - other->y_;
    float dz = z_ - other->z_;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vec3 *Vec3::add(const Vec3 *other) const {
    if (!other) throw eve::Exception("Vec3.add: other is null");
    return new Vec3(x_ + other->x_, y_ + other->y_, z_ + other->z_);
}

Vec3 *Vec3::sub(const Vec3 *other) const {
    if (!other) throw eve::Exception("Vec3.sub: other is null");
    return new Vec3(x_ - other->x_, y_ - other->y_, z_ - other->z_);
}

Vec3 *Vec3::scale(float s) const { return new Vec3(x_ * s, y_ * s, z_ * s); }

Vec3 *Vec3::lerpTo(const Vec3 *other, float t) const {
    if (!other) throw eve::Exception("Vec3.lerpTo: other is null");
    return new Vec3(x_ + (other->x_ - x_) * t, y_ + (other->y_ - y_) * t,
                    z_ + (other->z_ - z_) * t);
}

Vec3 *Vec3::clone() const { return new Vec3(x_, y_, z_); }

}  // namespace eve::math
