#include "animation/SpriteSheet.h"

#include "common/Exception.h"
#include "graphics/Quad.h"

namespace eve::animation {

void SpriteSheet::checkIndex(int index) const {
    if (index < 0 || index >= getFrameCount())
        throw Exception("SpriteSheet: frame index %d out of range (count=%d)", index,
                        getFrameCount());
}

int SpriteSheet::addFrame(const std::string &name, int x, int y, int w, int h) {
    return addFrameTrimmed(name, x, y, w, h, w, h, 0, 0);
}

int SpriteSheet::addFrameTrimmed(const std::string &name, int x, int y, int w, int h,
                                 int sourceW, int sourceH, int offsetX, int offsetY) {
    if (w <= 0 || h <= 0) throw Exception("SpriteSheet.addFrame: width/height must be > 0");
    Frame f;
    f.name = name.empty() ? ("frame" + std::to_string(frames_.size())) : name;
    if (byName_.count(f.name))
        throw Exception("SpriteSheet.addFrame: duplicate frame name '%s'", f.name.c_str());
    f.x = x;
    f.y = y;
    f.w = w;
    f.h = h;
    f.sourceW = sourceW;
    f.sourceH = sourceH;
    f.offsetX = offsetX;
    f.offsetY = offsetY;
    int idx = static_cast<int>(frames_.size());
    byName_[f.name] = idx;
    frames_.push_back(std::move(f));
    return idx;
}

int SpriteSheet::getFrameSourceWidth(int i) const { checkIndex(i); return frames_[size_t(i)].sourceW; }
int SpriteSheet::getFrameSourceHeight(int i) const { checkIndex(i); return frames_[size_t(i)].sourceH; }
int SpriteSheet::getFrameOffsetX(int i) const { checkIndex(i); return frames_[size_t(i)].offsetX; }
int SpriteSheet::getFrameOffsetY(int i) const { checkIndex(i); return frames_[size_t(i)].offsetY; }

int SpriteSheet::setGrid(int columns, int rows, int frameW, int frameH, int margin, int spacing,
                         int originX, int originY) {
    if (columns <= 0 || rows <= 0)
        throw Exception("SpriteSheet.setGrid: columns/rows must be > 0");
    if (frameW <= 0 || frameH <= 0)
        throw Exception("SpriteSheet.setGrid: frameW/frameH must be > 0");
    if (margin < 0 || spacing < 0)
        throw Exception("SpriteSheet.setGrid: margin/spacing must be >= 0");

    clear();
    int added = 0;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            int x = originX + margin + col * (frameW + spacing);
            int y = originY + margin + row * (frameH + spacing);
            addFrame("", x, y, frameW, frameH);
            ++added;
        }
    }
    return added;
}

void SpriteSheet::clear() {
    frames_.clear();
    byName_.clear();
}

int SpriteSheet::findFrame(const std::string &name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? -1 : it->second;
}

std::string SpriteSheet::getFrameName(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].name;
}

int SpriteSheet::getFrameX(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].x;
}

int SpriteSheet::getFrameY(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].y;
}

int SpriteSheet::getFrameWidth(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].w;
}

int SpriteSheet::getFrameHeight(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].h;
}

void SpriteSheet::applyToQuad(graphics::Quad *quad, int frameIndex) const {
    if (!quad) throw Exception("SpriteSheet.applyToQuad: quad is null");
    checkIndex(frameIndex);
    const Frame &f = frames_[static_cast<size_t>(frameIndex)];
    quad->setViewport(f.x, f.y, f.w, f.h);
}

SpriteSheet *SpriteSheet::clone() const {
    auto *copy = new SpriteSheet();
    copy->frames_ = frames_;
    copy->byName_ = byName_;
    copy->texture_ = texture_;
    return copy;
}

}  // namespace eve::animation
