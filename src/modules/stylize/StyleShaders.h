#pragma once
#include "common/Export.h"


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
EVENGINE_API_WORLD const StyleDefinition *findStyleDefinition(const std::string &style);

/** Built-in style ids accepted by string APIs. */
/** @brief True when known style. */
EVENGINE_API_WORLD bool isKnownStyle(const std::string &style);
/** @brief Style count. */
int styleCount();
/** @brief Style id at. */
std::string styleIdAt(int index);

/** Feature flags: "post" | "mesh" | "cpu" | "depth" | "normal" | "gbuffer". */
/** @brief Style supports. */
EVENGINE_API_WORLD bool styleSupports(const std::string &style, const std::string &feature);

/** Built-in post param name table (for tooling / UI introspection). */
/** @brief Style param count. */
EVENGINE_API_WORLD int                       styleParamCount(const std::string &style);
/** @brief Style param name. */
std::string styleParamName(const std::string &style, int index);
/** @brief Finds style parameter. */
EVENGINE_API_WORLD const StyleParameterDesc *findStyleParameter(const std::string &style, const std::string &name);
/** @brief Style parameter at. */
EVENGINE_API_WORLD const StyleParameterDesc *styleParameterAt(const std::string &style, int index);

/** Declare + seed default push-constant uniforms for a post style shader. */
/** @brief Binds post uniforms. */
void bindPostUniforms(graphics::Shader *shader, const std::string &style);

/** Declare + seed defaults for a mesh style shader. */
/** @brief Binds mesh uniforms. */
EVENGINE_API_WORLD void bindMeshUniforms(graphics::Shader *shader, const std::string &style);

/** Create a 2D post-process Shader from embedded SPIR-V (owned by Graphics). */
/** @brief Creates post shader. */
graphics::Shader *createPostShader(graphics::Graphics *gfx, const std::string &style);

/**
 * Create a 3D mesh Shader for styles that have object-space variants.
 * cartoon → reuse graphics mesh3d_toon SPIR-V; ink → ink_mesh; others → nullptr.
 */
/** @brief Creates mesh shader. */
EVENGINE_API_WORLD graphics::Shader *createMeshShader(graphics::Graphics *gfx, const std::string &style);

}  // namespace eve::stylize
