#pragma once

#include <string>

namespace eve::graphics {

class Graphics;
class Shader;

/** @brief Built-in Kajiya-Kay hair/fur card shader helpers. */
namespace hair {

/** @brief Create the default hair shader (alpha-blended, anisotropic specular). Owned by Graphics. */
Shader *createShader(Graphics *gfx);

/** @brief Bind default push-constant knobs on an existing hair shader. */
void bindDefaults(Shader *shader);

/** @brief Push-constant parameter names (same order as mesh3d_hair.frag). */
int paramCount();
std::string paramName(int index);

}  // namespace hair

}  // namespace eve::graphics
