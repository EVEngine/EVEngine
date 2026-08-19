#pragma once

#include "common/Module.h"

#include <string>

namespace ssq {
class Class;
class Table;
}

namespace eve::virtualgeometry {

class VirtualGeometryRenderer;

/**
 * @brief Virtual-geometry module — Nanite-style virtualized geometry rendering on top
 * of the active Graphics backend (Vulkan). See docs/dev/VIRTUAL_GEOMETRY.md.
 *
 * Script:
 *   vg <- eve.VirtualGeometry()
 *   r <- vg.newRenderer()
 *   r.buildIcosphere(4)
 */
class VirtualGeometry : public Module {
public:
    Module_REG(VirtualGeometry);
    VirtualGeometry() = default;
    ~VirtualGeometry() override = default;

    /** @brief True when the active Graphics backend can run virtual geometry. */
    bool isAvailable() const;

    /** @brief Create a virtual-geometry renderer (holds GPU buffers). */
    VirtualGeometryRenderer *newRenderer();
};

}  // namespace eve::virtualgeometry
