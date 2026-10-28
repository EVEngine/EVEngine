#pragma once
#include "common/Export.h"


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
class EVENGINE_API_WORLD StylePass {
public:
    /** @brief Style pass. */
    StylePass(const std::string &style, graphics::Shader *shader);
    /** @brief Style pass. */
    ~StylePass() = default;

    StylePass(const StylePass &) = delete;
    StylePass &operator=(const StylePass &) = delete;

    /** @brief Returns the style. */
    std::string getStyle() const { return style_; }
    /** @brief Returns the shader. */
    graphics::Shader *getShader() const { return shader_; }
    /** @brief Returns the stage. */
    std::string getStage() const;
    /** @brief Returns the priority. */
    int getPriority() const { return desc_.priority; }
    /** @brief Sets the priority. */
    void setPriority(int priority) { desc_.priority = priority; }
    /** @brief Requires input. */
    bool requiresInput(const std::string &input) const;

    /** @brief True when param. */
    bool hasParam(const std::string &name) const;
    /** @brief Sets the float. */
    void setFloat(const std::string &name, float value);
    /** @brief Returns the float. */
    float getFloat(const std::string &name) const;

    /** @brief Advance time-driven knobs (watercolor warp / ink jitter). */
    void setTime(float seconds);
    /** @brief Returns the time. */
    float getTime() const;

    /**
     * @brief Apply style into the currently bound canvas / screen.
     * Automatically uploads texel size + screen size uniforms.
     */
    void apply(graphics::Graphics *gfx, graphics::Texture *source);
    /** @brief Applies canvas. */
    void applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source);

    /**
     * @brief Apply into an explicit destination canvas (restores previous canvas bind).
     * Preferred hook for chains / tooling that manage ping-pong targets.
     */
    void applyTo(graphics::Graphics *gfx, graphics::Texture *source, graphics::Canvas *dest);
    /** @brief Applies canvas to. */
    void applyCanvasTo(graphics::Graphics *gfx, graphics::Canvas *source, graphics::Canvas *dest);

private:
    void uploadScreenUniforms(int width, int height);

    std::string style_;
    graphics::Shader *shader_ = nullptr;  // owned by Graphics
    graphics::PostEffectDesc desc_;
};

}  // namespace eve::stylize
