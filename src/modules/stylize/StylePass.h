#pragma once

#include <string>

namespace eve::graphics {
class Canvas;
class Graphics;
class Shader;
class Texture;
}  // namespace eve::graphics

namespace eve::stylize {

/**
 * One stylized post-process pass bound to a style id (cartoon/watercolor/ink/pixel).
 * Draws a full-quad of the source texture through a style fragment shader.
 *
 * Script: created via `stylize.newPass("watercolor")`.
 * Parameter knobs use string names (engine convention — no enums).
 */
class StylePass {
public:
    StylePass(const std::string &style, graphics::Shader *shader);
    ~StylePass() = default;

    StylePass(const StylePass &) = delete;
    StylePass &operator=(const StylePass &) = delete;

    std::string getStyle() const { return style_; }
    graphics::Shader *getShader() const { return shader_; }

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /** Advance time-driven knobs (watercolor warp / ink jitter). */
    void setTime(float seconds);
    float getTime() const;

    /**
     * Apply style: sample `source` (Texture or Canvas color buffer) and draw a
     * fullscreen quad into the currently bound canvas / screen.
     * Automatically uploads texel size + screen size uniforms.
     */
    void apply(graphics::Graphics *gfx, graphics::Texture *source);
    void applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source);

private:
    void uploadScreenUniforms(int width, int height);

    std::string style_;
    graphics::Shader *shader_ = nullptr;  // owned by Graphics
};

}  // namespace eve::stylize
