#pragma once

#include <glm/mat4x4.hpp>

namespace eve::graphics
{

class Graphics;

/**
 * Drawable base (textures, meshes, canvases).
 *
 * Occlusion follows the same idea as mesh shadow casting: a drawable may
 * contribute a black silhouette to a volumetric light occlusion map via
 * drawOcclusion(). Defaults to casting occlusion (opt-out with setCastOcclusion).
 */
class Drawable {
public:
    virtual ~Drawable() {}

    /**
	 * Draws the object with the specified transformation matrix.
	 **/
    virtual void draw(Graphics* gfx, const glm::mat4& matrix) const = 0;

    /** When false, skipped by Graphics::drawOcclusion / volumetric occluder passes. */
    bool getCastOcclusion() const { return castOcclusion_; }
    void setCastOcclusion(bool cast) { castOcclusion_ = cast; }

    /**
     * Draw as an opaque black silhouette for volumetric occlusion maps
     * (screen-space god rays / light shafts). Default is a no-op; Texture /
     * Mesh override with 2D black rects (alpha preserved for textures).
     */
    virtual void drawOcclusion(Graphics* gfx, const glm::mat4& matrix) const {
        (void)gfx;
        (void)matrix;
    }

private:
    bool castOcclusion_ = true;
};

} // namespace eve::graphics
