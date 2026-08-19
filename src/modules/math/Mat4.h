#pragma once

#include <glm/mat4x4.hpp>

namespace eve::math {

class Vec2;
class Vec3;

/** @brief Column-major 4x4 matrix wrapping glm::mat4. */
class Mat4 {
public:
    Mat4();
    explicit Mat4(const glm::mat4 &m);

    void identity();
    void translate(float x, float y, float z);
    void rotateX(float radians);
    void rotateY(float radians);
    void rotateZ(float radians);
    void scale(float sx, float sy, float sz);

    /** this = this * other (column-vector convention). */
    void multiply(const Mat4 *other);
    Mat4 *multiplied(const Mat4 *other) const;

    Vec3 *transformVec3(const Vec3 *v) const;
    Vec2 *transformPoint2(const Vec2 *v) const;

    /** @brief Column-major element 0..15. */
    float get(int index) const;
    void  set(int index, float value);

    Mat4 *clone() const;

    const glm::mat4 &raw() const { return m_; }
    glm::mat4       &raw() { return m_; }

private:
    glm::mat4 m_{1.f};
};

}  // namespace eve::math
