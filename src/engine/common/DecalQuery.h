#pragma once

#include "common/Export.h"

#include <string>

namespace eve {

/**
 * @brief Runtime decal query surface (provided by the decal module).
 *
 * Lets higher modules (editor / devtools) drive decals without linking the
 * decal module. The albedo texture handle is intentionally opaque here so the
 * common layer stays graphics-free; providers reinterpret it as
 * graphics::Texture*.
 */
class EVENGINE_API IDecalQuery {
public:
    static constexpr const char* capabilityName = "IDecalQuery";

    virtual ~IDecalQuery() = default;

    /**
     * @brief Spawn a decal at (x,y,z) facing (nx,ny,nz). `albedoTexture` is a
     * graphics::Texture* (opaque for the common layer). Returns id (>0) or 0.
     */
    virtual int project(float x, float y, float z, float nx, float ny, float nz,
                        void *albedoTexture, const std::string &kind, float size, float depth,
                        bool randomYaw, int seed, float fadeIn, float lifetime, float fadeOut) = 0;
    virtual bool remove(int id) = 0;
    virtual void clearAll() = 0;
    virtual int count() = 0;
    virtual void setLimit(const std::string &kind, int limit) = 0;
};

}  // namespace eve
