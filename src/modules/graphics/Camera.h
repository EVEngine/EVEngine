#pragma once

#include "graphics/Transform.h"

namespace eve::graphics
{

/** @brief 2D/3D 摄像机基类：持有变换栈并暴露投影矩阵。 */
class Camera : public Transform {
public:
    /** @brief 当前投影矩阵。 */
    virtual const glm::mat4 &getProjection() const;
};

} // namespace eve::graphics
