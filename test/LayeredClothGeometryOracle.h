#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace cloth_test_geometry {
// Independent double-precision, end-pose distance oracle. Uses plane projection,
// segment/plane intersections and boundary minima, not the GPU barycentric code.
using Triangle = std::array<glm::dvec3, 3>;
inline double segmentDistance(glm::dvec3 p, glm::dvec3 x, glm::dvec3 y) {
    const auto   d       = y - x;
    const double length2 = glm::dot(d, d);
    return glm::length(p - (x + d * (length2 > 1e-24 ? std::clamp(glm::dot(p - x, d) / length2, 0.0, 1.0) : 0.0)));
}
inline bool insideTriangle(glm::dvec3 p, const Triangle& t, glm::dvec3 normal) {
    for (int i = 0; i < 3; i++)
        if (glm::dot(glm::cross(t[size_t((i + 1) % 3)] - t[size_t(i)], p - t[size_t(i)]), normal) < -1e-12)
            return false;
    return true;
}
inline double pointDistance(glm::dvec3 p, const Triangle& t) {
    double best = 1e100;
    for (int i = 0; i < 3; i++) best = std::min(best, segmentDistance(p, t[size_t(i)], t[size_t((i + 1) % 3)]));
    const auto   normal  = glm::cross(t[1] - t[0], t[2] - t[0]);
    const double length2 = glm::dot(normal, normal);
    if (length2 > 1e-24) {
        const double distance   = glm::dot(p - t[0], normal);
        const auto   projection = p - normal * (distance / length2);
        if (insideTriangle(projection, t, normal)) best = std::min(best, std::abs(distance) / std::sqrt(length2));
    }
    return best;
}
inline double faceDistance(const Triangle& a, const Triangle& b) {
    double best = 1e100;
    for (int i = 0; i < 3; i++) best = std::min({best, pointDistance(a[size_t(i)], b), pointDistance(b[size_t(i)], a)});
    for (int i = 0; i < 3; i++) {
        const auto   x = a[size_t(i)], d = a[size_t((i + 1) % 3)] - x;
        const auto   normal = glm::cross(b[1] - b[0], b[2] - b[0]);
        const double rate   = glm::dot(normal, d);
        if (std::abs(rate) > 1e-14) {
            const double time = glm::dot(normal, b[0] - x) / rate;
            if (time >= 0 && time <= 1 && insideTriangle(x + d * time, b, normal)) return 0;
        }
        // Repeat the crossing test in the opposite direction.
        const auto   u = b[size_t(i)], v = b[size_t((i + 1) % 3)] - u;
        const auto   otherNormal = glm::cross(a[1] - a[0], a[2] - a[0]);
        const double otherRate   = glm::dot(otherNormal, v);
        if (std::abs(otherRate) > 1e-14) {
            const double time = glm::dot(otherNormal, a[0] - u) / otherRate;
            if (time >= 0 && time <= 1 && insideTriangle(u + v * time, a, otherNormal)) return 0;
        }
        for (int j = 0; j < 3; j++) {
            const auto   q = b[size_t(j)], e = b[size_t((j + 1) % 3)] - q, r = x - q;
            const double dd = glm::dot(d, d), ee = glm::dot(e, e), de = glm::dot(d, e), dr = glm::dot(d, r),
                         er  = glm::dot(e, r);
            const double det = dd * ee - de * de;
            if (det > 1e-24) {
                const double s = (de * er - ee * dr) / det, t = (dd * er - de * dr) / det;
                if (s >= 0 && s <= 1 && t >= 0 && t <= 1) best = std::min(best, glm::length(x + d * s - q - e * t));
            }
            best = std::min({best, segmentDistance(x, q, q + e), segmentDistance(x + d, q, q + e),
                             segmentDistance(q, x, x + d), segmentDistance(q + e, x, x + d)});
        }
    }
    return best;
}
inline double sheetSeparation(const std::vector<float>& pose) {
    double    minimum       = 1e100;
    const int indices[2][3] = {{0, 2, 1}, {2, 3, 1}};
    for (int first = 0; first < 4; first++)
        for (int second = first + 1; second < 4; second++)
            for (const auto& f : indices)
                for (const auto& s : indices) {
                    Triangle a, b;
                    for (int i = 0; i < 3; i++)
                        for (int k = 0; k < 3; k++) {
                            a[size_t(i)][k] = pose[size_t((first * 4 + f[i]) * 4 + k)];
                            b[size_t(i)][k] = pose[size_t((second * 4 + s[i]) * 4 + k)];
                        }
                    minimum = std::min(minimum, faceDistance(a, b));
                }
    return minimum;
}
}  // namespace cloth_test_geometry
