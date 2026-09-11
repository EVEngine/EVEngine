#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::physics {

class World;
class World3D;
class Fluid2D;
class DistanceField3D;

/**
 * @brief Physics module — Box2D (2D) + Box3D (3D) rigid bodies and SPH fluid.
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
     * @return Owning nullable world pointer; the caller is responsible for deleting the wrapper.
     * @ownership Ownership transfers to the caller; the Physics module does not retain the wrapper.
     * @lifetime Valid until caller destruction; call World::destroy() before releasing backend resources.
     * @thread Create and use on the owning physics thread.
     * @reentrancy The factory invokes no callbacks; do not re-enter physics mutation while using the result.
     */
    World *newWorld(float gravityX, float gravityY, bool sleep = true);

    /**
     * @brief Create a 3D Box3D physics world.
     * Coordinates are meters (Box3D native); +Y is up by convention.
     * @param gravityX gravity X in m/s²
     * @param gravityY gravity Y in m/s²
     * @param gravityZ gravity Z in m/s²
     * @param sleep allow sleeping bodies
     * @return Owning nullable world pointer; the caller is responsible for deleting the wrapper.
     * @ownership Ownership transfers to the caller; the Physics module does not retain the wrapper.
     * @lifetime Valid until caller destruction; call World3D::destroy() before releasing backend resources.
     * @thread Create and use on the owning physics thread.
     * @reentrancy The factory invokes no callbacks; do not re-enter physics mutation while using the result.
     */
    World3D *newWorld3D(float gravityX, float gravityY, float gravityZ, bool sleep = true);

    /**
     * @brief Create a regular 3D signed-distance grid for map collision queries.
     * @return Owning nullable field pointer transferred to the caller.
     * @ownership The caller owns the returned field and must delete it after use.
     * @lifetime Valid until caller destruction; it is not retained by Physics.
     * @thread Create and use on the owning physics thread.
     * @reentrancy The factory invokes no callbacks.
     */
    DistanceField3D *newDistanceField3D(int width, int height, int depth, float cellSize,
                                        float originX = 0.f, float originY = 0.f,
                                        float originZ = 0.f, float outsideDistance = 1e6f);

    /**
     * @brief Creates an interactive 2D particle fluid in pixel space.
     * @param capacity maximum number of particles (>= 1)
     * @return Owning nullable fluid pointer transferred to the caller.
     * @ownership The caller owns the returned fluid and must delete it after use.
     * @lifetime Valid until caller destruction; it is not retained by Physics.
     * @thread Create and use on the owning physics thread.
     * @reentrancy The factory invokes no callbacks.
     */
    Fluid2D *newFluid2D(int capacity = 512);

private:
    float meter_ = 30.f;
};

}  // namespace eve::physics
