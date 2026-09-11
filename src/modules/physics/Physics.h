#pragma once

#include "common/Module.h"

namespace eve::physics {

class World;
class World3D;
class Cloth;
class Cloth3D;
class SoftBody3D;
class SoftBody3DRenderer;
class ClothGPU;
class Fluid2D;
class DistanceField3D;

/**
 * @brief Physics module — Box2D (2D) + Box3D (3D) rigid bodies, plus interactive
 * 2D/3D cloth (Verlet, self-collision, fold limit, body collision) and SPH fluid.
 * Script: `physics <- eve.Physics(); world <- physics.newWorld(0, 900);`
 *         `world3 <- physics.newWorld3D(0, -9.8, 0);`
 *         `cloth3 <- physics.newCloth3D(18, 12, 0.5, 0, 5, 0);`
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
     * @brief Create a Verlet cloth grid (top row pinned).
     * @param cols columns (>= 2)
     * @param rows rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left X in pixels
     * @param originY top-left Y in pixels
     * @return Owning nullable cloth pointer transferred to the caller.
     * @ownership The caller owns the returned cloth and must delete it after use.
     * @lifetime Valid until caller destruction; it is not retained by Physics.
     * @thread Create and use on the owning physics thread.
     * @reentrancy The factory invokes no callbacks.
     */
    Cloth *newCloth(int cols, int rows, float spacing, float originX, float originY);

    /**
     * @brief Create a Verlet cloth grid in 3D meter space (+Y up, grid in XZ).
     * @param cols columns (>= 2), along +X
     * @param rows rows (>= 2), along +Z
     * @param spacing particle spacing in meters
     * @param originX top-left X (meters)
     * @param originY top-left Y (meters)
     * @param originZ top-left Z (meters)
     * @return Owning nullable cloth pointer transferred to the caller.
     * @ownership The caller owns the returned cloth and must delete it after use.
     * @lifetime Valid until caller destruction; it is not retained by Physics.
     * @thread Create and use on the owning physics thread.
     * @reentrancy The factory invokes no callbacks.
     */
    Cloth3D *newCloth3D(int cols, int rows, float spacing, float originX, float originY,
                        float originZ);

    /**
     * @brief Compatibility factory for a volumetric shape-matching soft body.
     *
     * The canonical C++ creation API is SoftBody3D::create(), which returns a
     * checked owning unique_ptr. This adapter exists for the script binding,
     * whose VM assumes ownership of the returned wrapper.
     * @return Owning pointer transferred to the script VM/caller.
     * @ownership The caller owns the returned object and must destroy it.
     * @lifetime Valid until caller destruction; a borrowed World3D must be cleared first.
     * @thread Create and use on the owning simulation thread.
     * @reentrancy No callbacks are invoked.
     * @throws eve::Exception when dimensions or coordinates are invalid.
     */
    SoftBody3D *newSoftBody3D(int cols, int rows, int layers, float spacing,
                              float originX, float originY, float originZ);

    /**
     * @brief Create a presentation satellite observing a soft-body runtime.
     * @param body Borrowed nullable runtime; clear it on the renderer before destroying the body.
     * @return Owning renderer transferred to the script VM/caller.
     * @ownership The caller owns the renderer; it never owns body or Graphics.
     * @thread Create and use on the owning render thread.
     */
    SoftBody3DRenderer *newSoftBody3DRenderer(SoftBody3D *body);

    /**
     * @brief Create a GPU-accelerated 2D Verlet cloth (compute shader backend).
     * Same interface as Cloth; requires the Gpgpu module and a compute-capable
     * Graphics backend (throws otherwise).
     * @param cols columns (>= 2)
     * @param rows rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left X (pixels)
     * @param originY top-left Y (pixels)
     * @return Owning nullable GPU cloth pointer transferred to the caller.
     * @ownership The caller owns the returned cloth; its GPU resources are released on destruction.
     * @lifetime Valid until caller destruction; it is not retained by Physics.
     * @thread Create and use on the owning physics/render thread.
     * @reentrancy The factory invokes no callbacks.
     */
    ClothGPU *newClothGPU(int cols, int rows, float spacing, float originX, float originY);

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
