#pragma once

#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Canvas;
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::stylize {

class StyleInstance;
class StylePass;

/**
 * @brief Compilable recipe of style instances sharing one pipeline stage.
 *
 * Instances are not owned. Compiled passes and transient ping-pong targets are
 * managed by the recipe/Graphics, so callers do not supply temporary canvases.
 */
class StyleRecipe {
public:
    StyleRecipe() = default;
    ~StyleRecipe();

    StyleRecipe(const StyleRecipe &) = delete;
    StyleRecipe &operator=(const StyleRecipe &) = delete;

    void clear();
    void add(StyleInstance *instance);
    int getStyleCount() const { return int(instances_.size()); }
    StyleInstance *getStyle(int index) const;

    void compile(graphics::Graphics *gfx);
    bool isCompiled() const { return compiled_; }
    std::string getStage() const { return stage_; }

    void apply(graphics::Graphics *gfx, graphics::Texture *source, graphics::Canvas *dest);
    void applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source, graphics::Canvas *dest);

private:
    void ensureScratch(graphics::Graphics *gfx, graphics::Canvas *dest);

    std::vector<StyleInstance *> instances_;
    std::vector<std::unique_ptr<StylePass>> passes_;
    graphics::Graphics *graphics_ = nullptr;
    graphics::Canvas *scratch_ = nullptr;
    int scratchWidth_ = 0;
    int scratchHeight_ = 0;
    std::string stage_;
    bool compiled_ = false;
};

}  // namespace eve::stylize
