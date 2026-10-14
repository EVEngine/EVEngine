#include "math/Steering.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eve::math::steering {
namespace {

bool finite(Vector2 value) { return std::isfinite(value.x) && std::isfinite(value.y); }
bool finite(Vector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

template <typename Vector>
double distance(Vector lhs, Vector rhs) {
    const double dx = static_cast<double>(lhs.x) - static_cast<double>(rhs.x);
    const double dy = static_cast<double>(lhs.y) - static_cast<double>(rhs.y);
    if constexpr (requires { lhs.z; }) {
        const double dz = static_cast<double>(lhs.z) - static_cast<double>(rhs.z);
        return std::hypot(dx, dy, dz);
    }
    return std::hypot(dx, dy);
}

template <typename Vector>
Vector scaledDirection(Vector from, Vector to, float magnitude) {
    if (!finite(from) || !finite(to) || !(magnitude > 0.f) || !std::isfinite(magnitude))
        return {};
    const double dx     = static_cast<double>(to.x) - static_cast<double>(from.x);
    const double dy     = static_cast<double>(to.y) - static_cast<double>(from.y);
    const double length = distance(from, to);
    if (!(length > 0.0) || !std::isfinite(length)) return {};
    if constexpr (requires { from.z; }) {
        const double dz = static_cast<double>(to.z) - static_cast<double>(from.z);
        return {static_cast<float>(dx / length * magnitude),
                static_cast<float>(dy / length * magnitude),
                static_cast<float>(dz / length * magnitude)};
    }
    return {static_cast<float>(dx / length * magnitude),
            static_cast<float>(dy / length * magnitude)};
}

template <typename Vector>
Vector seekImpl(Vector position, Vector target, float maxSpeed) {
    return scaledDirection(position, target, maxSpeed);
}

template <typename Vector>
Vector arriveImpl(Vector position, Vector target, float maxSpeed, float slowRadius,
                  float stopRadius) {
    if (!finite(position) || !finite(target) || !std::isfinite(slowRadius) ||
        !std::isfinite(stopRadius) || slowRadius <= stopRadius || stopRadius < 0.f)
        return {};
    const double targetDistance = distance(position, target);
    if (targetDistance <= stopRadius) return {};
    const double speed = static_cast<double>(maxSpeed) *
                         std::min(1.0, (targetDistance - stopRadius) /
                                           (static_cast<double>(slowRadius) - stopRadius));
    return scaledDirection(position, target, static_cast<float>(speed));
}

template <typename Vector>
Vector separationImpl(Vector position, std::span<const Vector> neighbors, float radius,
                      float maxAcceleration) {
    if (!finite(position) || !std::isfinite(radius) || !std::isfinite(maxAcceleration) ||
        radius <= 0.f || maxAcceleration <= 0.f)
        return {};
    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    for (const Vector neighbor : neighbors) {
        if (!finite(neighbor)) continue;
        const double neighborDistance = distance(position, neighbor);
        if (neighborDistance > 0.0 && neighborDistance < radius) {
            const double weight = (radius - neighborDistance) / (radius * neighborDistance);
            sumX += (static_cast<double>(position.x) - neighbor.x) * weight;
            sumY += (static_cast<double>(position.y) - neighbor.y) * weight;
            if constexpr (requires { position.z; })
                sumZ += (static_cast<double>(position.z) - neighbor.z) * weight;
        }
    }
    const double sumLength = std::hypot(sumX, sumY, sumZ);
    const double scale = sumLength > maxAcceleration ? maxAcceleration / sumLength : 1.0;
    if constexpr (requires { position.z; })
        return {static_cast<float>(sumX * scale), static_cast<float>(sumY * scale),
                static_cast<float>(sumZ * scale)};
    return {static_cast<float>(sumX * scale), static_cast<float>(sumY * scale)};
}

template <typename Vector>
int pathTargetImpl(Vector position, std::span<const Vector> points, int current,
                   float tolerance) {
    if (!finite(position) || points.empty()) return -1;
    if (std::ranges::any_of(points, [](Vector point) { return !finite(point); })) return -1;
    current = std::clamp(current, 0, static_cast<int>(points.size() - 1));
    const float acceptedTolerance =
        std::isfinite(tolerance) ? std::max(0.f, tolerance) : 0.f;
    while (current + 1 < static_cast<int>(points.size()) &&
           distance(points[current], position) <= acceptedTolerance)
        ++current;
    return current;
}

template <typename Vector>
Vector avoidImpl(Vector position, Vector velocity, Vector obstacle, float obstacleRadius,
                 float lookAhead, float maxAcceleration) {
    if (!finite(position) || !finite(velocity) || !finite(obstacle) ||
        !std::isfinite(obstacleRadius) || !std::isfinite(lookAhead) ||
        !std::isfinite(maxAcceleration) || obstacleRadius <= 0.f || lookAhead < 0.f ||
        maxAcceleration <= 0.f)
        return {};
    const Vector offset = scaledDirection(Vector{}, velocity, lookAhead);
    const double predictedX = static_cast<double>(position.x) + offset.x;
    const double predictedY = static_cast<double>(position.y) + offset.y;
    const double predictedZ = [&] {
        if constexpr (requires { position.z; }) return static_cast<double>(position.z) + offset.z;
        return 0.0;
    }();
    const double overlapX = predictedX - obstacle.x;
    const double overlapY = predictedY - obstacle.y;
    const double overlapZ = [&] {
        if constexpr (requires { obstacle.z; }) return predictedZ - obstacle.z;
        return 0.0;
    }();
    const double overlapLength = std::hypot(overlapX, overlapY, overlapZ);
    if (overlapLength > obstacleRadius) return {};
    if (overlapLength > 0.0) {
        if constexpr (requires { position.z; })
            return {static_cast<float>(overlapX / overlapLength * maxAcceleration),
                    static_cast<float>(overlapY / overlapLength * maxAcceleration),
                    static_cast<float>(overlapZ / overlapLength * maxAcceleration)};
        return {static_cast<float>(overlapX / overlapLength * maxAcceleration),
                static_cast<float>(overlapY / overlapLength * maxAcceleration)};
    }
    const Vector oppositeVelocity = [&] {
        if constexpr (requires { velocity.z; })
            return Vector{-velocity.x, -velocity.y, -velocity.z};
        return Vector{-velocity.x, -velocity.y};
    }();
    const Vector fallback = scaledDirection(Vector{}, oppositeVelocity, maxAcceleration);
    if (distance(Vector{}, fallback) > 0.0) return fallback;
    if constexpr (requires { position.z; }) return Vector{maxAcceleration, 0.f, 0.f};
    return Vector{maxAcceleration, 0.f};
}

}  // namespace

Vector2 seek(Vector2 position, Vector2 target, float maxSpeed) {
    return seekImpl(position, target, maxSpeed);
}
Vector3 seek(Vector3 position, Vector3 target, float maxSpeed) {
    return seekImpl(position, target, maxSpeed);
}
Vector2 flee(Vector2 position, Vector2 target, float maxSpeed) {
    return seekImpl(target, position, maxSpeed);
}
Vector3 flee(Vector3 position, Vector3 target, float maxSpeed) {
    return seekImpl(target, position, maxSpeed);
}
Vector2 arrive(Vector2 position, Vector2 target, float maxSpeed, float slowRadius,
               float stopRadius) {
    return arriveImpl(position, target, maxSpeed, slowRadius, stopRadius);
}
Vector3 arrive(Vector3 position, Vector3 target, float maxSpeed, float slowRadius,
               float stopRadius) {
    return arriveImpl(position, target, maxSpeed, slowRadius, stopRadius);
}
Vector2 separation(Vector2 position, std::span<const Vector2> neighbors, float radius,
                   float maxAcceleration) {
    return separationImpl(position, neighbors, radius, maxAcceleration);
}
Vector3 separation(Vector3 position, std::span<const Vector3> neighbors, float radius,
                   float maxAcceleration) {
    return separationImpl(position, neighbors, radius, maxAcceleration);
}
int pathTarget(Vector2 position, std::span<const Vector2> points, int current,
               float tolerance) {
    return pathTargetImpl(position, points, current, tolerance);
}
int pathTarget(Vector3 position, std::span<const Vector3> points, int current,
               float tolerance) {
    return pathTargetImpl(position, points, current, tolerance);
}
Vector2 avoid(Vector2 position, Vector2 velocity, Vector2 obstacle, float obstacleRadius,
              float lookAhead, float maxAcceleration) {
    return avoidImpl(position, velocity, obstacle, obstacleRadius, lookAhead, maxAcceleration);
}
Vector3 avoid(Vector3 position, Vector3 velocity, Vector3 obstacle, float obstacleRadius,
              float lookAhead, float maxAcceleration) {
    return avoidImpl(position, velocity, obstacle, obstacleRadius, lookAhead, maxAcceleration);
}

}  // namespace eve::math::steering
