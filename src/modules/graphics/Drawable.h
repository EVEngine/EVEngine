#pragma once

#include <glm/mat4x4.hpp>

namespace eve {

namespace graphics
{

class Drawable {
public:
    virtual ~Drawable() {}
    
    /**
	 * Draws the object with the specified transformation matrix.
	 **/
    virtual void draw(Graphics* gfx, const glm::mat4& matrix) = 0;
};

} // namespace graphics
} // namespace eve