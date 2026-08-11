#pragma once

#include "common/Module.h"

namespace eve::physics {

class World;
class Cloth;
class Fluid;

/**
 * Physics module — Box2D rigid bodies plus interactive cloth / SPH fluid.
 * Script: `physics <- eve.Physics(); world <- physics.newWorld(0, 900);`
 */
class Physics : public Module {
public:
    Module_REG(Physics);
    Physics() = default;
    ~Physics() override = default;

    /** Pixels per meter (default 30, same as LÖVE). */
    void  setMeter(float pixelsPerMeter);
    float getMeter() const;

    /**
     * Create a physics world.
     * @param gravityX gravity X in pixels/s²
     * @param gravityY gravity Y in pixels/s²
     * @param sleep allow sleeping bodies
     */
    World *newWorld(float gravityX, float gravityY, bool sleep = true);

    /**
     * Create a Verlet cloth grid (top row pinned).
     * @param cols columns (>= 2)
     * @param rows rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left X in pixels
     * @param originY top-left Y in pixels
     */
    Cloth *newCloth(int cols, int rows, float spacing, float originX, float originY);

    /**
     * Create an SPH fluid container with particle capacity.
     * @param capacity max particles (>= 1)
     */
    Fluid *newFluid(int capacity = 512);

private:
    float meter_ = 30.f;
};

}  // namespace eve::physics
