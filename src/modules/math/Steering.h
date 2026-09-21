#pragma once

#include "common/Export.h"

#include <span>

namespace eve::math::steering {

/**
 * @brief Stateless steering vector algorithms shared by 2D and 3D callers.
 *
 * All functions are thread-safe, retain no arguments, and return a neutral zero value for
 * non-finite or invalid scalar input unless another return contract is documented.
 */

/** @brief Plain 2D value used by the stateless steering calculations. */
struct EVENGINE_API_FOUNDATION_INLINE Vector2 {
    float x = 0.f;
    float y = 0.f;
};

/** @brief Plain 3D value used by the stateless steering calculations. */
struct EVENGINE_API_FOUNDATION_INLINE Vector3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief Returns a velocity of at most maxSpeed directed from position to target. */
EVENGINE_API_FOUNDATION Vector2 seek(Vector2 position, Vector2 target, float maxSpeed);
/** @brief Returns a velocity of at most maxSpeed directed from position to target. */
EVENGINE_API_FOUNDATION Vector3 seek(Vector3 position, Vector3 target, float maxSpeed);

/** @brief Returns a velocity of at most maxSpeed directed away from target. */
EVENGINE_API_FOUNDATION Vector2 flee(Vector2 position, Vector2 target, float maxSpeed);
/** @brief Returns a velocity of at most maxSpeed directed away from target. */
EVENGINE_API_FOUNDATION Vector3 flee(Vector3 position, Vector3 target, float maxSpeed);

/** @brief Slows toward target inside slowRadius and stops inside stopRadius. */
EVENGINE_API_FOUNDATION Vector2 arrive(Vector2 position, Vector2 target, float maxSpeed, float slowRadius,
               float stopRadius);
/** @brief Slows toward target inside slowRadius and stops inside stopRadius. */
EVENGINE_API_FOUNDATION Vector3 arrive(Vector3 position, Vector3 target, float maxSpeed, float slowRadius,
               float stopRadius);

/**
 * @brief Computes bounded separation acceleration from a borrowed neighbor snapshot.
 * @param neighbors Borrowed only for this call; the function retains no references.
 * @cost Linear in the number of neighbors.
 */
EVENGINE_API_FOUNDATION Vector2 separation(Vector2 position, std::span<const Vector2> neighbors, float radius,
                   float maxAcceleration);
/**
 * @brief Computes bounded separation acceleration from a borrowed neighbor snapshot.
 * @param neighbors Borrowed only for this call; the function retains no references.
 * @cost Linear in the number of neighbors.
 */
EVENGINE_API_FOUNDATION Vector3 separation(Vector3 position, std::span<const Vector3> neighbors, float radius,
                   float maxAcceleration);

/**
 * @brief Selects the current path point, advancing across points within tolerance.
 * @return A valid point index, or -1 when points is empty or contains a non-finite point.
 * @cost Linear in the number of points advanced during this call.
 */
EVENGINE_API_FOUNDATION int pathTarget(Vector2 position, std::span<const Vector2> points, int current,
               float tolerance);
/**
 * @brief Selects the current path point, advancing across points within tolerance.
 * @return A valid point index, or -1 when points is empty or contains a non-finite point.
 * @cost Linear in the number of points advanced during this call.
 */
EVENGINE_API_FOUNDATION int pathTarget(Vector3 position, std::span<const Vector3> points, int current,
               float tolerance);

/** @brief Computes avoidance acceleration away from a predicted obstacle overlap. */
EVENGINE_API_FOUNDATION Vector2 avoid(Vector2 position, Vector2 velocity, Vector2 obstacle, float obstacleRadius,
              float lookAhead, float maxAcceleration);
/** @brief Computes avoidance acceleration away from a predicted obstacle overlap. */
EVENGINE_API_FOUNDATION Vector3 avoid(Vector3 position, Vector3 velocity, Vector3 obstacle, float obstacleRadius,
              float lookAhead, float maxAcceleration);

}  // namespace eve::math::steering
