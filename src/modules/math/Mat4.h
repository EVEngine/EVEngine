#pragma once
#include "common/Export.h"


#include <glm/mat4x4.hpp>

namespace eve::math {

class Vec2;
class Vec3;

/** @brief Column-major 4x4 matrix wrapping glm::mat4. */
class EVENGINE_API_FOUNDATION Mat4 {
public:
    Mat4();
    /** @brief Wraps an existing glm::mat4. */
    explicit Mat4(const glm::mat4 &m);

    /** @brief Resets to the identity matrix. */
    void identity();
    /** @brief Post-multiplies a translation. */
    void translate(float x, float y, float z);
    /** @brief Post-multiplies a rotation about X (radians). */
    void rotateX(float radians);
    /** @brief Post-multiplies a rotation about Y (radians). */
    void rotateY(float radians);
    /** @brief Post-multiplies a rotation about Z (radians). */
    void rotateZ(float radians);
    /** @brief Scales by scalar s. @ownership Caller deletes. */
    void scale(float sx, float sy, float sz);

    /** this = this * other (column-vector convention). */
    /** @brief Post-multiplies by other (column-vector convention). */
    void multiply(const Mat4 *other);
    Mat4 *multiplied(const Mat4 *other) const;

    Vec3 *transformVec3(const Vec3 *v) const;
    Vec2 *transformPoint2(const Vec2 *v) const;

    /** @brief Column-major element 0..15. */
    float get(int index) const;
    /** @brief Sets all components. */
    void  set(int index, float value);

    Mat4 *clone() const;

    const glm::mat4 &raw() const { return m_; }
    glm::mat4       &raw() { return m_; }

private:
    glm::mat4 m_{1.f};
};

}  // namespace eve::math
