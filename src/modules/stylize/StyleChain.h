#pragma once

#include <string>
#include <vector>

namespace eve::graphics {
class Canvas;
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::stylize {

class StylePass;

/**
 * Ordered multi-pass stylize pipeline (extension point).
 *
 * Future NPR often needs ping-pong chains (separable bleed, outline then shade,
 * depth-aware contour, etc.). This type owns the sequencing contract; built-in
 * styles today are still single-pass, but callers can already compose them.
 *
 * Passes are NOT owned — create via Stylize::newPass / newPassFromShader.
 *
 * Script: `chain <- stylize.newChain(); chain.add(passA); chain.add(passB);`
 */
class StyleChain {
public:
    StyleChain() = default;
    ~StyleChain() = default;

    StyleChain(const StyleChain &) = delete;
    StyleChain &operator=(const StyleChain &) = delete;

    void clear();
    void add(StylePass *pass);
    int getPassCount() const { return int(passes_.size()); }
    StylePass *getPass(int index) const;

    /**
     * Run passes in order into `dest`.
     * When more than one pass is present, `temp` is required for ping-pong
     * (same size as dest, sampleable via getTexture()).
     */
    void apply(graphics::Graphics *gfx, graphics::Texture *source, graphics::Canvas *dest,
               graphics::Canvas *temp = nullptr);

    void applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source, graphics::Canvas *dest,
                     graphics::Canvas *temp = nullptr);

private:
    std::vector<StylePass *> passes_;
};

}  // namespace eve::stylize
