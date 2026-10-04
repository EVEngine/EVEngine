#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"

namespace eve::physics {

class GeometryCollectionInstance;
class GeometryCollectionRenderer;

/**
 * @brief Optional presentation satellite for geometry-collection destruction.
 *
 * Owns no instances; factories transfer renderer ownership to the caller/script VM.
 */
class EVENGINE_API_DOMAINS DestructionFx final : public Module {
public:
    Module_REG(DestructionFx);

    /**
     * @brief Create a presentation renderer that borrows an instance.
     * @param instance Borrowed geometry-collection instance; must be non-null.
     * @ownership Caller owns the returned renderer. instance remains borrowed.
     * @lifetime Renderer must not outlive instance unless setInstance(null) first.
     */
    [[nodiscard("check geometry-collection renderer creation")]]
    eve::Result<GeometryCollectionRenderer*> createRenderer(GeometryCollectionInstance* instance);

    /**
     * @brief Script facade that projects creation failure to an exception.
     * @ownership Script VM owns the returned renderer.
     * @lifetime Same as createRenderer.
     */
    GeometryCollectionRenderer* createRendererScript(GeometryCollectionInstance* instance);
};

}  // namespace eve::physics
