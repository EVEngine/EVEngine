#include "stylize/StyleRecipe.h"

#include "stylize/StyleChain.h"
#include "stylize/StyleInstance.h"
#include "stylize/StylePass.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"

#include <algorithm>

namespace eve::stylize {

StyleRecipe::~StyleRecipe() = default;

void StyleRecipe::clear() {
    instances_.clear();
    passes_.clear();
    stage_.clear();
    compiled_ = false;
}

void StyleRecipe::add(StyleInstance* instance) {
    if (!instance) throw eve::Exception("StyleRecipe.add: null instance");
    instances_.push_back(instance);
    compiled_ = false;
}

StyleInstance* StyleRecipe::getStyle(int index) const {
    if (index < 0 || index >= int(instances_.size())) return nullptr;
    return instances_[size_t(index)];
}

void StyleRecipe::compile(graphics::Graphics* gfx) {
    if (!gfx) throw eve::Exception("StyleRecipe.compile: null graphics");
    if (instances_.empty()) throw eve::Exception("StyleRecipe.compile: no styles");

    if (graphics_ != gfx) {
        scratch_       = nullptr;
        scratchWidth_  = 0;
        scratchHeight_ = 0;
    }
    std::stable_sort(instances_.begin(), instances_.end(),
                     [](StyleInstance* a, StyleInstance* b) { return a->getPriority() < b->getPriority(); });
    stage_ = instances_.front()->getStage();
    passes_.clear();
    for (StyleInstance* instance : instances_) {
        if (instance->getStage() != stage_)
            throw eve::Exception("StyleRecipe.compile: cannot mix stages '%s' and '%s'", stage_.c_str(),
                                 instance->getStage().c_str());
        passes_.emplace_back(instance->newPass(gfx));
    }
    graphics_ = gfx;
    compiled_ = true;
}

void StyleRecipe::ensureScratch(graphics::Graphics* gfx, graphics::Canvas* dest) {
    if (passes_.size() < 2) return;
    const int width  = dest->getWidth();
    const int height = dest->getHeight();
    if (graphics_ != gfx || !scratch_ || scratchWidth_ != width || scratchHeight_ != height) {
        scratch_       = gfx->newCanvas(width, height);
        scratchWidth_  = width;
        scratchHeight_ = height;
        graphics_      = gfx;
    }
}

void StyleRecipe::apply(graphics::Graphics* gfx, graphics::Texture* source, graphics::Canvas* dest) {
    if (!compiled_) throw eve::Exception("StyleRecipe.apply: compile first");
    if (graphics_ != gfx) throw eve::Exception("StyleRecipe.apply: compiled for another Graphics");
    if (!source || !dest) throw eve::Exception("StyleRecipe.apply: null source or destination");
    ensureScratch(gfx, dest);

    StyleChain chain;
    for (const auto& pass : passes_) chain.add(pass.get());
    chain.apply(gfx, source, dest, scratch_);
}

void StyleRecipe::applyCanvas(graphics::Graphics* gfx, graphics::Canvas* source, graphics::Canvas* dest) {
    if (!source || !source->getTexture()) throw eve::Exception("StyleRecipe.applyCanvas: source has no texture");
    apply(gfx, source->getTexture(), dest);
}

}  // namespace eve::stylize
