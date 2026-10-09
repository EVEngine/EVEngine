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
class EVENGINE_API_FOUNDATION_INLINE IDecalQuery {
public:
    static constexpr const char* capabilityName = "IDecalQuery";

    /** @brief I decal query. */
    virtual ~IDecalQuery() = default;

    /**
     * @brief Spawn a decal at (x,y,z) facing (nx,ny,nz). `albedoTexture` is a
     * graphics::Texture* (opaque for the common layer). Returns id (>0) or 0.
     */
    virtual int project(float x, float y, float z, float nx, float ny, float nz,
                        void *albedoTexture, const std::string &kind, float size, float depth,
                        bool randomYaw, int seed, float fadeIn, float lifetime, float fadeOut) = 0;
    /** @brief Removes . */
    virtual bool remove(int id) = 0;
    /** @brief Clears all. */
    virtual void clearAll() = 0;
    /** @brief Returns the number of . */
    virtual int count() = 0;
    /** @brief Sets the limit. */
    virtual void setLimit(const std::string &kind, int limit) = 0;
};

}  // namespace eve
