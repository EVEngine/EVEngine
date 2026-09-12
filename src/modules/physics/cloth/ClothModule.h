#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::physics {
class Cloth;
class Cloth3D;
class ClothGPU;
class ClothModel;
}  // namespace eve::physics

namespace eve::cloth {

/** @brief Script-facing owner and factory for 2D, 3D, and GPU cloth runtimes. */
class Cloth : public eve::Module {
public:
    Module_REG(Cloth);

    /**
     * @brief Create a caller-owned 2D cloth grid in pixel space.
     * @return Owning pointer; the caller must destroy the cloth after use.
     * @ownership Ownership transfers to the caller; the module retains no pointer.
     * @lifetime Valid until caller destruction.
     * @thread Create and use on the owning physics thread.
     * @reentrancy Invokes no callbacks.
     */
    eve::physics::Cloth* newCloth(int cols, int rows, float spacing, float originX, float originY);
    /**
     * @brief Create a caller-owned 3D cloth grid in meter space.
     * @return Owning pointer; the caller must destroy the cloth after use.
     * @ownership Ownership transfers to the caller; the module retains no pointer.
     * @lifetime Valid until caller destruction.
     * @thread Create and use on the owning physics thread.
     * @reentrancy Invokes no callbacks.
     */
    eve::physics::Cloth3D* newCloth3D(int cols, int rows, float spacing, float originX, float originY, float originZ);
    /** @brief Create an independent 3D runtime by copying a validated model. */
    [[nodiscard]] std::unique_ptr<eve::physics::Cloth3D> createCloth(const eve::physics::ClothModel& model);
    /**
     * @brief Create a caller-owned Vulkan-compute 2D cloth grid.
     * @return Owning pointer; the caller must destroy the cloth after use.
     * @ownership Ownership transfers to the caller; the module retains no pointer.
     * @lifetime Valid until caller destruction and while its graphics backend remains available.
     * @thread Create and use on the owning render thread.
     * @reentrancy Invokes no callbacks.
     */
    eve::physics::ClothGPU* newClothGPU(int cols, int rows, float spacing, float originX, float originY);
};

}  // namespace eve::cloth
