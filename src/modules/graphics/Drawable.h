#pragma once

#include "graphics/Mat4Param.h"

namespace eve::graphics {

class Graphics;

/**
 * @brief Drawable base (textures, meshes, canvases).
 *
 * Occlusion follows the same idea as mesh shadow casting: a drawable may
 * contribute a black silhouette to a volumetric light occlusion map via
 * drawOcclusion(). Defaults to casting occlusion (opt-out with setCastOcclusion).
 */
class Drawable {
public:
    /** @brief Releases Drawable resources. */
    virtual ~Drawable() {}

    /**
     * @brief Draws the object with the specified transformation matrix.
     * @param gfx Borrowed graphics backend for the draw.
     * @param matrix Column-major 4x4 (typically a `glm::mat4`).
     **/
    virtual void draw(Graphics* gfx, Mat4Param matrix) const = 0;

    /** @brief When false, skipped by Graphics::drawOcclusion / volumetric occluder passes. */
    bool getCastOcclusion() const { return castOcclusion_; }
    /** @brief Sets the cast occlusion. */
    void setCastOcclusion(bool cast) { castOcclusion_ = cast; }

    /**
     * @brief Draw as an opaque black silhouette for volumetric occlusion maps
     * (screen-space god rays / light shafts). Default is a no-op; Texture /
     * Mesh override with 2D black rects (alpha preserved for textures).
     */
    virtual void drawOcclusion(Graphics* gfx, Mat4Param matrix) const {
        (void)gfx;
        (void)matrix;
    }

private:
    bool castOcclusion_ = true;
};

}  // namespace eve::graphics
