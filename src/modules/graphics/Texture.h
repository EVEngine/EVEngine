#pragma once

#include "graphics/Drawable.h"
#include "graphics/Quad.h"
#include "graphics/TextureSampler.h"
#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {

class Graphics;

/**
 * @brief GPU texture created via Graphics::newTexture.
 * Owns GPU resources through an opaque backend handle.
 */
class Texture : public Drawable {
public:
    Texture();
    ~Texture() override;

    void draw(Graphics *gfx, const glm::mat4 &matrix) const override;
    /** @brief Black silhouette using texture alpha (volumetric occlusion map). */
    void drawOcclusion(Graphics *gfx, const glm::mat4 &matrix) const override;

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getPixelWidth() const { return pixelWidth; }
    int getPixelHeight() const { return pixelHeight; }
    int getMipmapCount() const { return mipmapCount; }
    const TextureSampler &getSampler() const { return sampler; }
    /** @brief Declare whether RGB is straight or already multiplied by alpha. */
    void setAlphaConvention(const std::string &value) {
        premultipliedAlpha_ = value == "premultiplied";
    }
    std::string getAlphaConvention() const {
        return premultipliedAlpha_ ? "premultiplied" : "straight";
    }
    bool hasPremultipliedAlpha() const { return premultipliedAlpha_; }

    /** Backend-private GPU object (vulkan::GpuTexture*). */
    void *gpuHandle = nullptr;

    int width = 0;
    int height = 0;
    int depth = 1;
    int layers = 1;
    int mipmapCount = 1;
    int pixelWidth = 0;
    int pixelHeight = 0;
    TextureSampler sampler{};
private:
    bool premultipliedAlpha_ = false;
};

}  // namespace eve::graphics
