#pragma once
#include "common/Export.h"


#include <cstdint>
#include <string>
#include "graphics/Drawable.h"
#include "graphics/TextureSampler.h"

namespace eve::graphics {

class Graphics;

/**
 * @brief GPU texture created via Graphics::newTexture.
 * Owns GPU resources through an opaque backend handle.
 */
class EVENGINE_API_BACKENDS Texture : public Drawable {
public:
    /** @brief Texture. */
    Texture();
    /** @brief Texture. */
    ~Texture() override;

    /** @brief Draws . */
    void draw(Graphics *gfx, const glm::mat4 &matrix) const override;
    /** @brief Black silhouette using texture alpha (volumetric occlusion map). */
    void drawOcclusion(Graphics *gfx, const glm::mat4 &matrix) const override;

    /** @brief Returns the width. */
    int getWidth() const;
    /** @brief Returns the height. */
    int getHeight() const;
    /** @brief Returns the pixel width. */
    int getPixelWidth() const;
    /** @brief Returns the pixel height. */
    int getPixelHeight() const;
    /** @brief Returns the mipmap count. */
    int getMipmapCount() const;
    /** @brief Returns the sampler. */
    const TextureSampler &getSampler() const { return sampler; }
    /** @brief Declare whether RGB is straight or already multiplied by alpha. */
    void setAlphaConvention(const std::string &value) {
        premultipliedAlpha_ = value == "premultiplied";
    }
    /** @brief Returns the alpha convention. */
    std::string getAlphaConvention() const {
        return premultipliedAlpha_ ? "premultiplied" : "straight";
    }
    /** @brief True when premultiplied alpha. */
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

    /** @brief Mark deferred file pixels. */
    void markDeferredFilePixels(Graphics *graphics);
    /** @brief Clears deferred file pixels. */
    void clearDeferredFilePixels();
    /** @brief True when deferred file pixels. */
    bool hasDeferredFilePixels() const { return filePixelsPending_; }

private:
    void realizeFilePixelsIfNeeded() const;
    Graphics *graphics_ = nullptr;
    bool filePixelsPending_ = false;
    bool premultipliedAlpha_ = false;
};

}  // namespace eve::graphics
