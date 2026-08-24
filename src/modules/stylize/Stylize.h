#pragma once

#include "common/Module.h"
#include "stylize/StyleChain.h"
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

class StyleInstance;
class StyleRecipe;

/**
 * @brief Stylized / NPR rendering module — stable public surface for future expansion.
 *
 * Built-in style ids (string, no enums):
 *   - "cartoon"    — cel post (+ mesh)
 *   - "watercolor" — paper / bleed post
 *   - "ink"        — ink-wash post (+ mesh)
 *   - "pixel"      — pixel-art post
 *   - "xray"       — depth-aware mesh technique
 *
 * Extension points (keep these stable):
 *   - newPass / newPassFromShader — wrap built-in or custom SPIR-V post shaders
 *   - newMeshShader — object-space variants (cartoon/ink today)
 *   - StyleChain — multi-pass ping-pong (separable blur, outline+shade, …)
 *   - supports(style, feature) — "post" | "mesh" | "cpu" | "gbuffer"
 *     ("gbuffer" is true when depth or normal inputs are required)
 *   - getStyleParam* — introspect push-constant knobs for tooling / UI
 *
 * Script: `stylize <- eve.Stylize();`
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

    /**
     * @brief Feature query. Known features:
     *   "post"    — StylePass / newPostShader
     *   "mesh"    — newMeshShader
     *   "cpu"     — processImage
     *   "depth" / "normal" — required scene inputs
     *   "gbuffer" — shorthand for either depth or normal
     */
    bool supports(const std::string &style, const std::string &feature) const;

    /** @brief Introspect built-in post param names (empty for unknown / custom-only ids). */
    int         getStyleParamCount(const std::string &style) const;
    std::string getStyleParamName(const std::string &style, int index) const;

    /** @brief Create a mutable parameter instance of an immutable style definition. */
    StyleInstance *newInstance(const std::string &style);
    /** @brief Create an empty compilable multi-style recipe. */
    StyleRecipe *newRecipe();

    /** @brief Post-process StylePass (shader owned by Graphics). */
    StylePass *newPass(graphics::Graphics *gfx, const std::string &style);

    /**
     * @brief Wrap an already-created post Shader (custom SPIR-V / future registered styles).
     * `styleId` is a label only; capability tables are not updated.
     * Shader must already declare its float uniforms.
     */
    StylePass *newPassFromShader(const std::string &styleId, graphics::Shader *shader);

    /** Empty multi-pass chain (caller adds StylePass*). */
    StyleChain *newChain();

    /** @brief Raw post Shader with defaults (owned by Graphics). */
    graphics::Shader *newPostShader(graphics::Graphics *gfx, const std::string &style);

    /** @brief Mesh3D Shader for cartoon/ink (owned by Graphics). */
    graphics::Shader *newMeshShader(graphics::Graphics *gfx, const std::string &style);

    /**
     * @brief CPU fallback stylization for ImageData (RGBA8).
     * Caller owns returned ImageData*.
     */
    image::ImageData *processImage(image::ImageData *src, const std::string &style);
};

}  // namespace eve::stylize
