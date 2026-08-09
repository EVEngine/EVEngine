#pragma once

#include "common/Module.h"
#include "stylize/StylePass.h"

#include <string>

namespace eve::graphics {
class Graphics;
class Shader;
}  // namespace eve::graphics

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::stylize {

/**
 * Stylized / NPR rendering module.
 *
 * Styles (string ids):
 *   - "cartoon"    — cel bands + posterize + Sobel outline (post + mesh)
 *   - "watercolor" — paper warp, bleed, edge darken, granulation (post)
 *   - "ink"        — ink-wash tones + silhouette (post + mesh)
 *   - "pixel"      — low-res snap + palette quantize + Bayer dither (post)
 *
 * Script: `stylize <- eve.Stylize();`
 *
 * Typical flow:
 *   1. Render scene into a Canvas
 *   2. `pass <- stylize.newPass("watercolor")`
 *   3. Bind screen / another canvas, then `pass.applyCanvas(gfx, src)`
 */
class Stylize : public Module {
public:
    Module_REG(Stylize);
    Stylize() = default;
    ~Stylize() override = default;

    int         getStyleCount() const;
    std::string getStyleId(int index) const;
    bool        hasStyle(const std::string &style) const;
    bool        hasMeshStyle(const std::string &style) const;

    /** Post-process StylePass (shader owned by Graphics). */
    StylePass *newPass(graphics::Graphics *gfx, const std::string &style);

    /** Raw post Shader with defaults (owned by Graphics). */
    graphics::Shader *newPostShader(graphics::Graphics *gfx, const std::string &style);

    /** Mesh3D Shader for cartoon/ink (owned by Graphics). */
    graphics::Shader *newMeshShader(graphics::Graphics *gfx, const std::string &style);

    /**
     * CPU fallback stylization for ImageData (RGBA8).
     * Useful for tests / tooling without a full GPU pass.
     * Supports: cartoon (posterize+edges), ink (tones), pixel (quantize+dither).
     * Watercolor CPU path approximates blur + paper grain.
     * Caller owns returned ImageData*.
     */
    image::ImageData *processImage(image::ImageData *src, const std::string &style);
};

}  // namespace eve::stylize
