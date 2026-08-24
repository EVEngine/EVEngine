#pragma once

#include "graphics/PostEffect.h"

#include <string>

namespace eve::graphics {
class Canvas;
class Graphics;
class Shader;
class Texture;
}  // namespace eve::graphics

namespace eve::stylize {

/**
 * @brief One stylized post-process pass bound to a style id (built-in or custom label).
 * Draws a full-quad of the source texture through a style fragment shader.
 *
 * Parameters use string names (engine convention — no enums).
 * Future GBuffer-aware styles may read extra textures via Graphics bindings;
 * this class stays the single entry for "run one NPR post step".
 */
class StylePass {
public:
    StylePass(const std::string &style, graphics::Shader *shader);
    ~StylePass() = default;

    StylePass(const StylePass &) = delete;
    StylePass &operator=(const StylePass &) = delete;

    std::string getStyle() const { return style_; }
    graphics::Shader *getShader() const { return shader_; }
    std::string getStage() const;
    int getPriority() const { return desc_.priority; }
    void setPriority(int priority) { desc_.priority = priority; }
    bool requiresInput(const std::string &input) const;

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /** @brief Advance time-driven knobs (watercolor warp / ink jitter). */
    void setTime(float seconds);
    float getTime() const;

    /**
     * @brief Apply style into the currently bound canvas / screen.
     * Automatically uploads texel size + screen size uniforms.
     */
    void apply(graphics::Graphics *gfx, graphics::Texture *source);
    void applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source);

    /**
     * @brief Apply into an explicit destination canvas (restores previous canvas bind).
     * Preferred hook for chains / tooling that manage ping-pong targets.
     */
    void applyTo(graphics::Graphics *gfx, graphics::Texture *source, graphics::Canvas *dest);
    void applyCanvasTo(graphics::Graphics *gfx, graphics::Canvas *source, graphics::Canvas *dest);

private:
    void uploadScreenUniforms(int width, int height);

    std::string style_;
    graphics::Shader *shader_ = nullptr;  // owned by Graphics
    graphics::PostEffectDesc desc_;
};

}  // namespace eve::stylize
