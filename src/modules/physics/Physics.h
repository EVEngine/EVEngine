#pragma once

#include "common/Module.h"

namespace eve::physics {

class World;

/**
 * Box2D physics module — factory + meter scale (pixels per meter).
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

private:
    float meter_ = 30.f;
};

}  // namespace eve::physics
