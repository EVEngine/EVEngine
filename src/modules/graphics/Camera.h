#pragma once

#include "graphics/Transform.h"

namespace eve::graphics
{

class Camera : public Transform {
public:
    virtual const glm::mat4 &getProjection() const;
};

} // namespace eve::graphics
