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

float length(Vector2 value) { return std::hypot(value.x, value.y); }
float length(Vector3 value) { return std::hypot(value.x, value.y, value.z); }

Vector2 subtract(Vector2 lhs, Vector2 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y}; }
Vector3 subtract(Vector3 lhs, Vector3 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vector2 scaled(Vector2 value, float magnitude) {
    const float valueLength = length(value);
    if (!(valueLength > 0.f) || !std::isfinite(valueLength) || !(magnitude > 0.f) ||
        !std::isfinite(magnitude))
        return {};
    return {value.x / valueLength * magnitude, value.y / valueLength * magnitude};
}

Vector3 scaled(Vector3 value, float magnitude) {
    const float valueLength = length(value);
    if (!(valueLength > 0.f) || !std::isfinite(valueLength) || !(magnitude > 0.f) ||
        !std::isfinite(magnitude))
        return {};
    return {value.x / valueLength * magnitude, value.y / valueLength * magnitude,
            value.z / valueLength * magnitude};
}

template <typename Vector>
Vector seekImpl(Vector position, Vector target, float maxSpeed) {
    if (!finite(position) || !finite(target)) return {};
    return scaled(subtract(target, position), maxSpeed);
}

template <typename Vector>
Vector arriveImpl(Vector position, Vector target, float maxSpeed, float slowRadius,
                  float stopRadius) {
    if (!finite(position) || !finite(target) || !std::isfinite(slowRadius) ||
        !std::isfinite(stopRadius) || slowRadius <= stopRadius || stopRadius < 0.f)
        return {};
    const Vector delta    = subtract(target, position);
    const float  distance = length(delta);
    if (distance <= stopRadius) return {};
    const float speed = maxSpeed * std::min(1.f, (distance - stopRadius) /
                                                    (slowRadius - stopRadius));
    return scaled(delta, speed);
}

template <typename Vector>
Vector separationImpl(Vector position, std::span<const Vector> neighbors, float radius,
                      float maxAcceleration) {
    if (!finite(position) || !std::isfinite(radius) || !std::isfinite(maxAcceleration) ||
        radius <= 0.f || maxAcceleration <= 0.f)
        return {};
    Vector sum{};
    for (const Vector neighbor : neighbors) {
        if (!finite(neighbor)) continue;
        const Vector delta    = subtract(position, neighbor);
        const float  distance = length(delta);
        if (distance > 0.f && distance < radius) {
            const float weight = (radius - distance) / (radius * distance);
            sum.x += delta.x * weight;
            sum.y += delta.y * weight;
            if constexpr (requires { sum.z; }) sum.z += delta.z * weight;
        }
    }
    return length(sum) > maxAcceleration ? scaled(sum, maxAcceleration) : sum;
}

template <typename Vector>
int pathTargetImpl(Vector position, std::span<const Vector> points, int current,
                   float tolerance) {
    if (!finite(position) || points.empty()) return -1;
    current = std::clamp(current, 0, static_cast<int>(points.size() - 1));
    const float acceptedTolerance =
        std::isfinite(tolerance) ? std::max(0.f, tolerance) : 0.f;
    while (current + 1 < static_cast<int>(points.size()) && finite(points[current]) &&
           length(subtract(points[current], position)) <= acceptedTolerance)
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
    const Vector offset    = scaled(velocity, lookAhead);
    Vector       predicted = position;
    predicted.x += offset.x;
    predicted.y += offset.y;
    if constexpr (requires { predicted.z; }) predicted.z += offset.z;
    if (length(subtract(predicted, obstacle)) > obstacleRadius) return {};
    return scaled(subtract(predicted, obstacle), maxAcceleration);
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
