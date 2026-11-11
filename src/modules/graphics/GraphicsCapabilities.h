#pragma once
/** @file GraphicsCapabilities.h
 * @brief graphics module-interface: render capture/probes and artifact presentation registration.
 * @remarks Runtime font/FontData metrics are a direct, required dependency of the text/atlas API in Graphics.h;
 * the manifest owns that dependency. Capability-out registration here remains IRenderCapture/IProcgenProbeSink and
 * the artifact provider. This dependency correction changes no ECS/render handles, domain events, Graphics script
 * bindings or tool endpoints; native tool consumers continue to use their common capability interfaces.
 */
#include "common/Export.h"


namespace eve::graphics {
/** @brief Registers graphics capabilities. */
EVENGINE_API_BACKENDS void registerGraphicsCapabilities();
/** @brief Registers the backend-neutral generated-artifact graphics provider. */
class Graphics;
/** @brief Registers graphics artifact provider. */
EVENGINE_API_BACKENDS void registerGraphicsArtifactProvider(Graphics* graphics);
/** @brief Detach a derived Graphics backend before its resources are destroyed. */
void detachGraphicsArtifactProvider(Graphics* graphics) noexcept;
}
