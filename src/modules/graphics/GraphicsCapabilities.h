#pragma once
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
