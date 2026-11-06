#pragma once
#include "common/RenderTypes.h"
namespace eve::particles {
/** @brief Owning CPU sprite appearance, with pixel rectangle, degree rotation, color and normalized atlas UV.
 * @remarks Contains no texture, shader, camera, ECS or simulation references. Rectangle placement follows the
 * live renderer, including its existing stretched-sprite anchor. Resource selection remains with the presenter.
 */
struct ParticleAppearance {
    float x = 0, y = 0, w = 0, h = 0, rotation = 0;
    Color color;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};
}  // namespace eve::particles
