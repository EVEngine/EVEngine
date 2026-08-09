#include "stylize/StyleChain.h"

#include "stylize/StylePass.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"

#include <glm/vec4.hpp>

namespace eve::stylize {

void StyleChain::clear() { passes_.clear(); }

void StyleChain::add(StylePass *pass) {
    if (!pass) throw eve::Exception("StyleChain.add: null pass");
    passes_.push_back(pass);
}

StylePass *StyleChain::getPass(int index) const {
    if (index < 0 || index >= int(passes_.size())) return nullptr;
    return passes_[size_t(index)];
}

void StyleChain::apply(graphics::Graphics *gfx, graphics::Texture *source, graphics::Canvas *dest,
                       graphics::Canvas *temp) {
    if (!gfx) throw eve::Exception("StyleChain.apply: null graphics");
    if (!source) throw eve::Exception("StyleChain.apply: null source");
    if (!dest) throw eve::Exception("StyleChain.apply: null dest");
    if (passes_.empty()) throw eve::Exception("StyleChain.apply: no passes");

    if (passes_.size() == 1) {
        passes_[0]->applyTo(gfx, source, dest);
        return;
    }
    if (!temp) throw eve::Exception("StyleChain.apply: temp canvas required for multi-pass");
    if (!temp->getTexture())
        throw eve::Exception("StyleChain.apply: temp canvas has no sampleable texture");
    if (!dest->getTexture())
        throw eve::Exception("StyleChain.apply: dest canvas has no sampleable texture");

    // Alternate temp / dest. Final result must live in dest.
    // n even: … → temp → dest
    // n odd : … → dest → temp, then blit temp → dest
    graphics::Texture *in = source;
    const int n = int(passes_.size());
    for (int i = 0; i < n; ++i) {
        graphics::Canvas *out = (i & 1) ? dest : temp;
        passes_[size_t(i)]->applyTo(gfx, in, out);
        in = out->getTexture();
        if (!in) throw eve::Exception("StyleChain.apply: intermediate texture missing");
    }
    if ((n & 1) != 0) {
        // Last write was temp — blit into dest (no style shader).
        graphics::Canvas *prev = gfx->getCanvas();
        gfx->setCanvas(dest);
        gfx->drawTexturedRect(temp->getTexture(), 0.f, 0.f, float(dest->getWidth()),
                              float(dest->getHeight()), glm::vec4(1.f, 1.f, 1.f, 1.f));
        gfx->setCanvas(prev);
    }
}

void StyleChain::applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source,
                             graphics::Canvas *dest, graphics::Canvas *temp) {
    if (!source) throw eve::Exception("StyleChain.applyCanvas: null source");
    graphics::Texture *tex = source->getTexture();
    if (!tex) throw eve::Exception("StyleChain.applyCanvas: source has no texture");
    apply(gfx, tex, dest, temp);
}

}  // namespace eve::stylize
