#pragma once
#include <glm/vec2.hpp>
#include "common/Result.h"

namespace eve::graphics {
class Graphics;
class Shader;
struct VegetationWindProfile;
struct VegetationWindState;

/** @brief Caller-owned tree prototype wind geometry matching Pcg ResourceProtoTree inputs. */
struct TreeWindProfile {
    /** @brief Object-space crown width and source prototype height; both must be positive. */
    glm::vec2 widthHeight{1, 1};
    /** @brief Pcg tree prototype bend factor. Zero disables deformation. */
    float bendFactor = 1;
};

/** @brief Create a backend-native tree Mesh3D shader using the engine PBR fragment stage.
 * @param graphics Borrowed initialized Graphics owner.
 * @return Graphics-owned shader pointer.
 * @throws eve::Exception for a null/uninitialized owner or backend pipeline failure.
 * @ownership The returned shader is owned by graphics and is main/render-thread affine.
 */
Shader* createTreeWindShader(Graphics* graphics);

/** @brief Atomically publish one validated tree wind snapshot to CPU-side shader parameters.
 * @param shader Borrowed tree shader with the canonical 16-float layout.
 * @param state Borrowed immutable global wind state.
 * @param vegetation Borrowed immutable shared PW material controls.
 * @param tree Borrowed immutable tree prototype dimensions and Pcg bend factor.
 * @param seconds Explicit finite simulation time; no wall clock is read.
 * @return Success or InvalidArgument. Failure preserves all shader parameter bytes.
 * @ownership Retains no references and invokes no callbacks. Call on the shader owner thread.
 */
[[nodiscard]] Result<void> applyTreeWind(Shader& shader, const VegetationWindState& state,
                                         const VegetationWindProfile& vegetation,
                                         const TreeWindProfile& tree, double seconds);
}  // namespace eve::graphics
