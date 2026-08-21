#pragma once

#include "common/Module.h"

namespace eve::physics {

class World;
class World3D;
class Cloth;
class Fluid;

/**
 * @brief Physics module — Box2D (2D) + Box3D (3D) rigid bodies, plus interactive cloth / SPH fluid.
 * Script: `physics <- eve.Physics(); world <- physics.newWorld(0, 900);`
 *         `world3 <- physics.newWorld3D(0, -9.8, 0);`
 */
class Physics : public Module {
public:
    Module_REG(Physics);
    Physics();
    ~Physics() override = default;

    /** @brief Pixels per meter for 2D Box2D worlds (default 30, same as LÖVE). */
    void  setMeter(float pixelsPerMeter);
    float getMeter() const;

    /**
     * @brief Create a 2D Box2D physics world.
     * @param gravityX gravity X in pixels/s²
     * @param gravityY gravity Y in pixels/s²
     * @param sleep allow sleeping bodies
     */
    World *newWorld(float gravityX, float gravityY, bool sleep = true);

    /**
     * @brief Create a 3D Box3D physics world.
     * Coordinates are meters (Box3D native); +Y is up by convention.
     * @param gravityX gravity X in m/s²
     * @param gravityY gravity Y in m/s²
     * @param gravityZ gravity Z in m/s²
     * @param sleep allow sleeping bodies
     */
    World3D *newWorld3D(float gravityX, float gravityY, float gravityZ, bool sleep = true);

    /**
     * @brief Create a Verlet cloth grid (top row pinned).
     * @param cols columns (>= 2)
     * @param rows rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left X in pixels
     * @param originY top-left Y in pixels
     */
    Cloth *newCloth(int cols, int rows, float spacing, float originX, float originY);

    /**
     * @brief Create an SPH fluid container with particle capacity.
     * @param capacity max particles (>= 1)
     */
    Fluid *newFluid(int capacity = 512);

private:
    float meter_ = 30.f;
};

}  // namespace eve::physics
