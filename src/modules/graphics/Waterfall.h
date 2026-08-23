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
class Waterfall {
public:
    explicit Waterfall(Graphics *gfx);
    ~Waterfall() = default;

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
    void setTime(float seconds);
    float getTime() const { return time_; }

    // --- Animation / material knobs ---
    /** @brief Fall speed of the water (scales the downward scroll). */
    void setFlowSpeed(float speed);
    float getFlowSpeed() const { return flowSpeed_; }

    /** @brief Amount of turbulence / white-water streak in the body. */
    void setTurbulence(float t);
    float getTurbulence() const { return turbulence_; }

    /** @brief How many layered falling streaks are drawn. */
    void setStreakCount(int count);
    int getStreakCount() const { return streakCount_; }

    /** @brief Horizontal stretch of the falling streaks (1 = circular, >1 elongated). */
    void setStreakScale(float scale);
    float getStreakScale() const { return streakScale_; }

    /** @brief Relative height (0..1) of the top foam lip and bottom splash bands. */
    void setTopFoam(float v);
    float getTopFoam() const { return topFoam_; }
    void setBottomFoam(float v);
    float getBottomFoam() const { return bottomFoam_; }
    void setFoamAmount(float v);
    float getFoamAmount() const { return foamAmount_; }

    void setWaterColor(float r, float g, float b);
    void setReflectionIntensity(float intensity);
    float getReflectionIntensity() const { return reflectionIntensity_; }
    void setSunIntensity(float intensity);
    float getSunIntensity() const { return sunIntensity_; }

    /** @brief Upload current params to the shader push constants. */
    void bindParams();

    /** @brief Draw the waterfall sheet (uses default mesh3d camera / lighting state). */
    void draw();

    Shader *getShader() const { return shader_; }
    Mesh *getMesh() const { return mesh_; }

    /** @brief Names of the push-constant parameters (for UI / inspection). */
    static int paramCount();
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
};

/** @brief Create the embedded waterfall fragment shader (owned by Graphics). */
Shader *newWaterfallShader(Graphics *gfx);

}  // namespace eve::graphics
