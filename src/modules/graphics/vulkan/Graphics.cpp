#include "graphics/vulkan/Graphics.h"


namespace eve::graphics::vulkan {

void Graphics::present() {}

void  Graphics::setViewportSize(int width, int height, int pixelwidth, int pixelheight) {}
void  Graphics::draw(eve::graphics::Graphics* gfx, const glm::mat4& matrix) const {}
void  Graphics::draw(Canvas* C, const glm::mat4& matrix) const {}
void  Graphics::clear(std::optional<Color> color, std::optional<int> stencil, std::optional<double> depth) {}
Color Graphics::getPixel(int x, int y) {
    return Color(0,0,0,0);
}

}  // namespace eve::graphics::vulkan