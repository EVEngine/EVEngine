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
 * Script: construct via `eve.RayTracing()`, then gate work on `isAvailable()`.
 *
 * When the GPU lacks RT extensions, factories still construct but every
 * fallible operation returns `StatusCode::Unsupported`.
 */
class EVENGINE_API_WORLD RayTracing : public eve::Module {
public:
    Module_REG(RayTracing);
    /** @brief Ray tracing. */
    RayTracing() = default;
    /** @brief Ray tracing. */
    ~RayTracing() override = default;

    /** @brief True when the active Graphics device enabled hardware RT. */
    bool isAvailable() const;

    /** @brief Snapshot of probed RT device capabilities. */
    RayTracingCaps caps() const;

    /**
     * @brief Borrow the process-wide IRayTracing provider, or nullptr when the
     * module is linked but Graphics has not initialized yet.
     * @ownership Non-owning borrow of the capability registry entry; do not delete.
     * @lifetime Valid until Graphics tears down the RT backend (or process exit).
     * @thread Render thread only.
     */
    IRayTracing *backend() const;
};

}  // namespace eve::graphics::raytracing
