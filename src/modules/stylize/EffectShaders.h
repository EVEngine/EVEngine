#pragma once

// Effect / material shaders ported from common Unity and Godot game projects,
// exposed through the same Style* runtime and Squirrel API as the built-in
// stylize recipes. Each effect is one GLSL fragment (embedded SPIR-V) plus a
// metadata table for tooling introspection.
//
// Mesh effects reuse graphics/mesh3d_toon.vert and read params from the push
// constant block; post effects are full-screen passes over the scene texture.

#include "stylize/StyleShaders.h"

namespace eve::graphics {
class Graphics;
class Shader;
}  // namespace eve::graphics

namespace eve::stylize {

/** @brief True when @p style names one of the ported effect shaders. */
bool isEffectStyle(const std::string& style);

/** @brief Effect-only StyleDefinition lookup, or nullptr when unknown. */
const StyleDefinition* findEffectDefinition(const std::string& style);
int                    effectStyleCount();
std::string            effectStyleIdAt(int index);

/** Param metadata for an effect style (empty for styles with none). */
int                       effectParamCount(const std::string& style);
std::string               effectParamName(const std::string& style, int index);
const StyleParameterDesc* findEffectParameter(const std::string& style, const std::string& name);
const StyleParameterDesc* effectParameterAt(const std::string& style, int index);

/** Declare + seed default push-constant uniforms. */
void bindEffectPostUniforms(graphics::Shader* shader, const std::string& style);
void bindEffectMeshUniforms(graphics::Shader* shader, const std::string& style);

/** Create a 2D post-process Shader from embedded SPIR-V. */
graphics::Shader* createEffectPostShader(graphics::Graphics* gfx, const std::string& style);

/** Create a 3D mesh Shader (mesh3d_toon.vert + effect fragment). */
graphics::Shader* createEffectMeshShader(graphics::Graphics* gfx, const std::string& style);

}  // namespace eve::stylize