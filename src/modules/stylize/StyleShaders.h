#pragma once

#include "graphics/PostEffect.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Shader;
}  // namespace eve::graphics

namespace eve::stylize {

/** @brief Immutable capability metadata for one built-in stylization recipe. */
struct StyleDefinition {
    const char *id;
    bool post;
    bool mesh;
    bool cpu;
    bool depth;
    bool normal;
    graphics::PostEffectStage stage;
    int priority;
};

/** @brief Tooling metadata for one user-facing float parameter. */
struct StyleParameterDesc {
    const char *id;
    float defaultValue;
    float minValue;
    float maxValue;
};

/** @brief Return a built-in definition, or nullptr when the id is unknown. */
const StyleDefinition *findStyleDefinition(const std::string &style);

/** Built-in style ids accepted by string APIs. */
bool isKnownStyle(const std::string &style);
int styleCount();
std::string styleIdAt(int index);

/** Feature flags: "post" | "mesh" | "cpu" | "depth" | "normal" | "gbuffer". */
bool styleSupports(const std::string &style, const std::string &feature);

/** Built-in post param name table (for tooling / UI introspection). */
int styleParamCount(const std::string &style);
std::string styleParamName(const std::string &style, int index);
const StyleParameterDesc *findStyleParameter(const std::string &style, const std::string &name);
const StyleParameterDesc *styleParameterAt(const std::string &style, int index);

/** Declare + seed default push-constant uniforms for a post style shader. */
void bindPostUniforms(graphics::Shader *shader, const std::string &style);

/** Declare + seed defaults for a mesh style shader. */
void bindMeshUniforms(graphics::Shader *shader, const std::string &style);

/** Create a 2D post-process Shader from embedded SPIR-V (owned by Graphics). */
graphics::Shader *createPostShader(graphics::Graphics *gfx, const std::string &style);

/**
 * Create a 3D mesh Shader for styles that have object-space variants.
 * cartoon → reuse graphics mesh3d_toon SPIR-V; ink → ink_mesh; others → nullptr.
 */
graphics::Shader *createMeshShader(graphics::Graphics *gfx, const std::string &style);

}  // namespace eve::stylize
