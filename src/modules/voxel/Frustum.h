#pragma once

#include <cmath>

namespace eve::voxel {

/** Six frustum planes in ax+by+cz+d >= 0 form (normals point inward). */
struct Frustum {
    float planes[6][4]{};

    /**
     * Extract from a column-major 4x4 view-projection matrix (RH + Vulkan ZO).
     * Layout matches glm::mat4 memory order.
     */
    static Frustum fromViewProjColumnMajor(const float *m16) {
        Frustum f;
        // Column-major → treat rows of transpose as m[col*4+row] access via
        // row-major view of columns: plane = row3 ± row{0,1,2} of the matrix.
        auto m = [&](int row, int col) -> float { return m16[col * 4 + row]; };
        auto set = [&](int i, float a, float b, float c, float d) {
            const float len = std::sqrt(a * a + b * b + c * c);
            if (len > 1e-8f) {
                f.planes[i][0] = a / len;
                f.planes[i][1] = b / len;
                f.planes[i][2] = c / len;
                f.planes[i][3] = d / len;
            } else {
                f.planes[i][0] = a;
                f.planes[i][1] = b;
                f.planes[i][2] = c;
                f.planes[i][3] = d;
            }
        };
        // left, right, bottom, top, near, far
        set(0, m(3, 0) + m(0, 0), m(3, 1) + m(0, 1), m(3, 2) + m(0, 2), m(3, 3) + m(0, 3));
        set(1, m(3, 0) - m(0, 0), m(3, 1) - m(0, 1), m(3, 2) - m(0, 2), m(3, 3) - m(0, 3));
        set(2, m(3, 0) + m(1, 0), m(3, 1) + m(1, 1), m(3, 2) + m(1, 2), m(3, 3) + m(1, 3));
        set(3, m(3, 0) - m(1, 0), m(3, 1) - m(1, 1), m(3, 2) - m(1, 2), m(3, 3) - m(1, 3));
        set(4, m(3, 0) + m(2, 0), m(3, 1) + m(2, 1), m(3, 2) + m(2, 2), m(3, 3) + m(2, 3));
        set(5, m(3, 0) - m(2, 0), m(3, 1) - m(2, 1), m(3, 2) - m(2, 2), m(3, 3) - m(2, 3));
        return f;
    }

    bool intersectsAABB(float minX, float minY, float minZ, float maxX, float maxY,
                        float maxZ) const {
        for (int i = 0; i < 6; ++i) {
            const float *p = planes[i];
            const float x = p[0] >= 0.f ? maxX : minX;
            const float y = p[1] >= 0.f ? maxY : minY;
            const float z = p[2] >= 0.f ? maxZ : minZ;
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.f) return false;
        }
        return true;
    }
};

}  // namespace eve::voxel
