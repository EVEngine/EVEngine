#pragma once
#include "common/Export.h"


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
class EVENGINE_API_WORLD StyleRecipe {
public:
    /** @brief Style recipe. */
    StyleRecipe() = default;
    /** @brief Style recipe. */
    ~StyleRecipe();

    StyleRecipe(const StyleRecipe&)            = delete;
    StyleRecipe& operator=(const StyleRecipe&) = delete;

    /** @brief Clears . */
    void           clear();
    /** @brief Adds . */
    void           add(StyleInstance* instance);
    /** @brief Returns the style count. */
    int            getStyleCount() const { return int(instances_.size()); }
    /** @brief Returns the style. */
    StyleInstance* getStyle(int index) const;

    /** @brief Compiles . */
    void        compile(graphics::Graphics* gfx);
    /** @brief True when compiled. */
    bool        isCompiled() const { return compiled_; }
    /** @brief Returns the stage. */
    std::string getStage() const { return stage_; }

    /** @brief Applies . */
    void apply(graphics::Graphics* gfx, graphics::Texture* source, graphics::Canvas* dest);
    /** @brief Applies canvas. */
    void applyCanvas(graphics::Graphics* gfx, graphics::Canvas* source, graphics::Canvas* dest);

private:
    void ensureScratch(graphics::Graphics* gfx, graphics::Canvas* dest);

    std::vector<StyleInstance*>             instances_;
    std::vector<std::unique_ptr<StylePass>> passes_;
    graphics::Graphics*                     graphics_      = nullptr;
    graphics::Canvas*                       scratch_       = nullptr;
    int                                     scratchWidth_  = 0;
    int                                     scratchHeight_ = 0;
    std::string                             stage_;
    bool                                    compiled_ = false;
};

}  // namespace eve::stylize
