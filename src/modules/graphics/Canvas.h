#pragma once

#include "graphics/Drawable.h"
// #include "image/ImageData.h"

#include <glm/vec4.hpp>
#include <optional>

typedef glm::vec4 Color;
namespace eve::graphics
{

class Canvas : public eve::graphics::Drawable
{
public:
    Canvas();
    ~Canvas();

    // draw current content on target Canvas
    virtual void draw(Canvas* C, const glm::mat4& matrix) const;


    virtual void clear(std::optional<Color> color, std::optional<int> stencil, std::optional<double> depth) = 0;
    // virtual image::ImageData* getImageData() = 0;
    // virtual image::ImageData* cloneImageData() = 0;

    virtual Color getPixel(int x, int y) = 0;
    

};



} // namespace eve::graphics
