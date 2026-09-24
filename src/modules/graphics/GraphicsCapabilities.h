#pragma once
#include "common/Export.h"


namespace eve::graphics {
EVENGINE_API_BACKENDS void registerGraphicsCapabilities();
/** @brief Registers the backend-neutral generated-artifact graphics provider. */
class Graphics;
EVENGINE_API_BACKENDS void registerGraphicsArtifactProvider(Graphics* graphics);
/** @brief Detach a derived Graphics backend before its resources are destroyed. */
void detachGraphicsArtifactProvider(Graphics* graphics) noexcept;
}
