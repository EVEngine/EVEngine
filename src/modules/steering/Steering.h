#pragma once
#include <string>
#include "common/Module.h"
namespace eve::steering {
/** @brief Plain 2D vector used by steering primitives. */
struct Vec2 {
    float x = 0, y = 0;
};
/** @brief Stateless, gameplay-neutral 2D steering math. */
class Steering : public Module {
public:
    Module_REG(Steering);
    /** @brief Desired velocity toward a target. */ static Vec2    seek(float x, float y, float tx, float ty,
                                                                        float maxSpeed);
    /** @brief Desired velocity away from a target. */ static Vec2 flee(float x, float y, float tx, float ty,
                                                                        float maxSpeed);
    /** @brief Desired velocity that slows inside slowRadius and stops inside stopRadius. */ static Vec2 arrive(
        float x, float y, float tx, float ty, float maxSpeed, float slowRadius, float stopRadius);
    /** @brief Separation acceleration from CSV neighbor coordinates formatted x:y,x:y. */ static Vec2 separation(
        float x, float y, const std::string& neighbors, float radius, float maxAcceleration);
    /** @brief Chooses the next path point from CSV x:y points and returns its index. */ static int pathTarget(
        float x, float y, const std::string& points, int current, float tolerance);
    /** @brief Avoidance acceleration away from a predicted obstacle position. */ static Vec2 avoid(
        float x, float y, float vx, float vy, float ox, float oy, float obstacleRadius, float lookAhead,
        float maxAcceleration);
    /** @brief JSON adapters for scripts. */ static std::string seekJson(float, float, float, float, float);
    static std::string                                          fleeJson(float, float, float, float, float);
    static std::string arriveJson(float, float, float, float, float, float, float);
    static std::string separationJson(float, float, const std::string&, float, float);
    static std::string avoidJson(float, float, float, float, float, float, float, float, float);
};
}  // namespace eve::steering
