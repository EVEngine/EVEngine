#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "graphics/IRayTracing.h"
#include "graphics/RayTracingCaps.h"

#include <cstdint>

namespace eve::graphics::raytracing {

/**
 * @brief Optional Vulkan KHR ray-tracing module.
 *
 * Script:
 *   rt <- eve.RayTracing()
 *   if (rt.isAvailable()) { ... }
 *
 * When the GPU lacks RT extensions, factories still construct but every
 * fallible operation returns `StatusCode::Unsupported`.
 */
class EVENGINE_API_WORLD RayTracing : public eve::Module {
public:
    Module_REG(RayTracing);
    RayTracing() = default;
    ~RayTracing() override = default;

    /** @brief True when the active Graphics device enabled hardware RT. */
    bool isAvailable() const;

    /** @brief Snapshot of probed RT device capabilities. */
    RayTracingCaps caps() const;

    /**
     * @brief Borrow the process-wide IRayTracing provider, or nullptr when the
     * module is linked but Graphics has not initialized yet.
     */
    IRayTracing *backend() const;
};

}  // namespace eve::graphics::raytracing
