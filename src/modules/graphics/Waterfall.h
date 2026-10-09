#pragma once

#include "graphics/Shader.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace eve::graphics {

class Graphics;
class Mesh;
class Texture;

/**
 * @brief Flowing waterfall (falling water sheet) rendered on a vertical plane.
 *
 * A custom Mesh3D fragment shader renders a vertical XY plane (facing +Z) as
 * falling water:
 *   - Water rushes downward, driven by a scrolling noise + layered sine streaks
 *     so the surface reads as a tumbling cascade rather than a flat plane.
 *   - Animated foam/white-crest band at the top lip (water spilling over) and a
 *     turbulent splash zone at the bottom where the sheet hits the pool.
 *   - Vertical velocity streaks stretch in the fall direction to sell the motion.
 *   - Reflects the sky via the environment cubemap (binding 3), Fresnel-weighted,
 *     plus a specular glint from the primary directional light.
 *
 * Parameters are packed into the shader push-constant block (data[0..31]); see
 * Waterfall::bindParams for the layout. Caller owns Waterfall*; its Mesh / Shader
 * are owned by Graphics.
 */
class EVENGINE_API_BACKENDS Waterfall {
public:
    /** @brief Waterfall. */
    explicit Waterfall(Graphics *gfx);
    /** @brief Waterfall. */
    ~Waterfall();

    Waterfall(const Waterfall &) = delete;
    Waterfall &operator=(const Waterfall &) = delete;

    /** @brief Build a vertical XY plane (facing +Z, Y-up world) sized w×h, UVs [0,1]². */
    void createSheet(float width, float height, int segX, int segY);

    /**
     * @brief Build a shaped water curtain with a convex cross-section and projecting lip.
     * @param width Nominal width of the curtain at its upper edge.
     * @param height Vertical fall distance.
     * @param segX Horizontal tessellation count.
     * @param segY Vertical tessellation count.
     * @param curveDepth Maximum forward bow at the curtain centre.
     * @param lipOverhang Additional forward projection at the upper lip.
     */
    void createCurvedSheet(float width, float height, int segX, int segY, float curveDepth,
                           float lipOverhang);

    /** @brief Advance the animation clock by dt seconds. */
    void update(float dt);
    /** @brief Sets the time. */
    void setTime(float seconds);
    /** @brief Returns the time. */
    float getTime() const { return time_; }

    // --- Animation / material knobs ---
    /** @brief Fall speed of the water (scales the downward scroll). */
    void setFlowSpeed(float speed);
    /** @brief Returns the flow speed. */
    float getFlowSpeed() const { return flowSpeed_; }

    /** @brief Amount of turbulence / white-water streak in the body. */
    void setTurbulence(float t);
    /** @brief Returns the turbulence. */
    float getTurbulence() const { return turbulence_; }

    /** @brief How many layered falling streaks are drawn. */
    void setStreakCount(int count);
    /** @brief Returns the streak count. */
    int getStreakCount() const { return streakCount_; }

    /** @brief Horizontal stretch of the falling streaks (1 = circular, >1 elongated). */
    void setStreakScale(float scale);
    /** @brief Returns the streak scale. */
    float getStreakScale() const { return streakScale_; }

    /** @brief Relative height (0..1) of the top foam lip and bottom splash bands. */
    void setTopFoam(float v);
    /** @brief Returns the top foam. */
    float getTopFoam() const { return topFoam_; }
    /** @brief Sets the bottom foam. */
    void setBottomFoam(float v);
    /** @brief Returns the bottom foam. */
    float getBottomFoam() const { return bottomFoam_; }
    /** @brief Sets the foam amount. */
    void setFoamAmount(float v);
    /** @brief Returns the foam amount. */
    float getFoamAmount() const { return foamAmount_; }

    /** @brief Sets the water color. */
    void setWaterColor(float r, float g, float b);
    /** @brief Sets the reflection intensity. */
    void setReflectionIntensity(float intensity);
    /** @brief Returns the reflection intensity. */
    float getReflectionIntensity() const { return reflectionIntensity_; }
    /** @brief Sets the sun intensity. */
    void setSunIntensity(float intensity);
    /** @brief Returns the sun intensity. */
    float getSunIntensity() const { return sunIntensity_; }

    /** @brief Upload current params to the shader push constants. */
    void bindParams();

    /** @brief Draw the waterfall sheet (uses default mesh3d camera / lighting state). */
    void draw();

    /** @brief Enable or disable inclusion in reflection-probe captures. */
    void setReflectionCaptureEnabled(bool enabled) { reflectionCaptureEnabled_ = enabled; }
    /** @brief Return whether this waterfall is included in reflection-probe captures. */
    bool getReflectionCaptureEnabled() const { return reflectionCaptureEnabled_; }
    /** @brief Set the reflection-capture visibility layer mask. */
    void setReflectionCaptureMask(uint32_t mask) { reflectionCaptureMask_ = mask; }
    /** @brief Return the reflection-capture visibility layer mask. */
    uint32_t getReflectionCaptureMask() const { return reflectionCaptureMask_; }

    /** @brief Returns the shader. */
    Shader *getShader() const { return shader_; }
    /** @brief Returns the mesh. */
    Mesh *getMesh() const { return mesh_; }

    /** @brief Names of the push-constant parameters (for UI / inspection). */
    static int paramCount();
    /** @brief Param name. */
    static std::string paramName(int index);

private:
    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    Mesh *mesh_ = nullptr;

    float time_ = 0.f;
    float flowSpeed_ = 1.4f;
    float turbulence_ = 0.6f;
    int streakCount_ = 4;
    float streakScale_ = 5.f;
    float topFoam_ = 0.06f;
    float bottomFoam_ = 0.12f;
    float foamAmount_ = 0.85f;
    float waterColor_[3] = {0.05f, 0.22f, 0.30f};
    float reflectionIntensity_ = 0.55f;
    float sunIntensity_ = 0.8f;
    uint64_t captureDrawerToken_ = 0;
    uint32_t reflectionCaptureMask_ = 0xffffffffu;
    bool reflectionCaptureEnabled_ = true;
};

/** @brief Create the embedded waterfall fragment shader (owned by Graphics). */
Shader *newWaterfallShader(Graphics *gfx);

}  // namespace eve::graphics
