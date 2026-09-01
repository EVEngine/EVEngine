#pragma once

namespace eve::graphics {
void registerGraphicsCapabilities();
/** @brief Registers the backend-neutral generated-artifact graphics provider. */
class Graphics;
void registerGraphicsArtifactProvider(Graphics* graphics);
/** @brief Detach a derived Graphics backend before its resources are destroyed. */
void detachGraphicsArtifactProvider(Graphics* graphics) noexcept;
}
