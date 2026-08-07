#include "math/Mat4.h"
#include "math/Vec2.h"
#include "math/Vec3.h"

#include "common/Exception.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace eve::math {

Mat4::Mat4() : m_(1.f) {}
Mat4::Mat4(const glm::mat4 &m) : m_(m) {}

void Mat4::identity() { m_ = glm::mat4(1.f); }

void Mat4::translate(float x, float y, float z) {
    m_ = glm::translate(m_, glm::vec3(x, y, z));
}

void Mat4::rotateX(float radians) { m_ = glm::rotate(m_, radians, glm::vec3(1.f, 0.f, 0.f)); }
void Mat4::rotateY(float radians) { m_ = glm::rotate(m_, radians, glm::vec3(0.f, 1.f, 0.f)); }
void Mat4::rotateZ(float radians) { m_ = glm::rotate(m_, radians, glm::vec3(0.f, 0.f, 1.f)); }

void Mat4::scale(float sx, float sy, float sz) {
    m_ = glm::scale(m_, glm::vec3(sx, sy, sz));
}

void Mat4::multiply(const Mat4 *other) {
    if (!other) throw eve::Exception("Mat4.multiply: other is null");
    m_ = m_ * other->m_;
}

Mat4 *Mat4::multiplied(const Mat4 *other) const {
    if (!other) throw eve::Exception("Mat4.multiplied: other is null");
    return new Mat4(m_ * other->m_);
}

Vec3 *Mat4::transformVec3(const Vec3 *v) const {
    if (!v) throw eve::Exception("Mat4.transformVec3: v is null");
    glm::vec4 r = m_ * glm::vec4(v->getX(), v->getY(), v->getZ(), 1.f);
    return new Vec3(r.x, r.y, r.z);
}

Vec2 *Mat4::transformPoint2(const Vec2 *v) const {
    if (!v) throw eve::Exception("Mat4.transformPoint2: v is null");
    glm::vec4 r = m_ * glm::vec4(v->getX(), v->getY(), 0.f, 1.f);
    return new Vec2(r.x, r.y);
}

float Mat4::get(int index) const {
    if (index < 0 || index > 15) throw eve::Exception("Mat4.get: index must be 0..15");
    return glm::value_ptr(m_)[index];
}

void Mat4::set(int index, float value) {
    if (index < 0 || index > 15) throw eve::Exception("Mat4.set: index must be 0..15");
    glm::value_ptr(m_)[index] = value;
}

Mat4 *Mat4::clone() const { return new Mat4(m_); }

}  // namespace eve::math
