#pragma once

#include "graphics/Drawable.h"

namespace eve::graphics
{

class Canvas : public eve::graphics::Drawable
{
public:
    Canvas();
    ~Canvas();

    virtual void draw(Graphics* gfx, const glm::mat4& matrix);
};



} // namespace eve::graphics
