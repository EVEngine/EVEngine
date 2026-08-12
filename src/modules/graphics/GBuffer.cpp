#include "graphics/GBuffer.h"

namespace eve::graphics {

bool GBuffer::isValid() const {
    return width_ > 0 && height_ > 0 && depth_ != nullptr && normal_ != nullptr;
}

bool GBuffer::hasBuffer(const std::string &name) const {
    if (name == "depth") return depth_ != nullptr;
    if (name == "normal") return normal_ != nullptr;
    if (name == "albedo") return albedo_ != nullptr;
    return false;
}

Texture *GBuffer::getBuffer(const std::string &name) const {
    if (name == "depth") return depth_;
    if (name == "normal") return normal_;
    if (name == "albedo") return albedo_;
    return nullptr;
}

void GBuffer::setTargets(int width, int height, Texture *depth, Texture *normal, Texture *albedo) {
    width_ = width;
    height_ = height;
    depth_ = depth;
    normal_ = normal;
    albedo_ = albedo;
}

void GBuffer::clear() {
    width_ = 0;
    height_ = 0;
    depth_ = nullptr;
    normal_ = nullptr;
    albedo_ = nullptr;
}

}  // namespace eve::graphics
