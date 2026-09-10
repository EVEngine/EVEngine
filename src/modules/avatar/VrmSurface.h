#pragma once
#include <memory>
#include <vector>
#include "avatar/VrmDocument.h"

namespace eve::graphics {
class Graphics;
class Texture;
class Shader;
class Material;
}  // namespace eve::graphics
namespace eve::avatar {
struct VrmAtlasRect {
    int x = 0, y = 0, w = 1, h = 1;
};
struct VrmSurface {
    VrmSurface();
    ~VrmSurface();
    std::weak_ptr<const void>           providerLifetime;
    VrmMaterial                         base, current;
    std::unique_ptr<graphics::Material> material;
    std::unique_ptr<graphics::Material> outlineMaterial;
    graphics::Shader*                   shader   = nullptr;
    graphics::Texture*                  metadata = nullptr;
    std::array<float, 2>                uvScale{1, 1}, uvOffset{};
    void                                update(float time);
};
/**
 * @brief Prepare an atlas using the provider's owning resource factory.
 * @ownership Graphics owns the result; caller releases it through that provider after use.
 * @lifetime Valid until explicit release or provider destruction; observe resourceLifetime().
 * @thread Render thread, no callbacks. Throws before publication on invalid image data.
 */
graphics::Texture&          buildVrmAtlas(graphics::Graphics& graphics, const VrmDocument& document,
                                          std::vector<VrmAtlasRect>& rectangles);
std::unique_ptr<VrmSurface> buildVrmSurface(graphics::Graphics& graphics, const VrmMaterial& data,
                                            graphics::Texture& atlas, const std::vector<VrmAtlasRect>& rectangles);
}  // namespace eve::avatar
