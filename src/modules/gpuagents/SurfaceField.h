#pragma once
#include "common/Export.h"

#include <glm/glm.hpp>

#include <vector>

namespace eve::gpuagents {

/**
 * @brief 2.5D surface + life-field textures for Life Network.
 *
 * Height/normal come from an orthographic surface capture (or a procedural plane).
 * Trail / nutrient / danger channels live in `lifeField` (RGBA float texels).
 */
class EVENGINE_API_DOMAINS SurfaceField {
public:
    glm::vec3 origin{-16.f, 0.f, -16.f};
    float     worldSize = 32.f;
    int       resolution = 64;

    /** @brief Height samples, resolution^2, row-major X then Z. */
    std::vector<float> height;
    /** @brief Packed normals as vec3 per texel. */
    std::vector<glm::vec3> normals;
    /** @brief Life field RGBA: R=trail, G=nutrient, B=danger, A=freshness. */
    std::vector<glm::vec4> lifeField;

    /** @brief Allocate grids and fill a flat plane at y = planeY. */
    void initFlat(const glm::vec3& originMin, float size, int res, float planeY = 0.f);

    /** @brief World XZ → texel UV in [0,1]. */
    glm::vec2 worldToUv(const glm::vec3& p) const;

    /** @brief Bilinear sample of height at world XZ. */
    float sampleHeight(float x, float z) const;

    /** @brief Bilinear sample of surface normal. */
    glm::vec3 sampleNormal(float x, float z) const;

    /** @brief Bilinear sample of life RGBA. */
    glm::vec4 sampleLife(float x, float z) const;

    /** @brief Project a world point onto the surface (Y from height). */
    glm::vec3 project(const glm::vec3& p) const;

    /**
     * @brief Advance trail deposit / decay / diffusion / danger for one fixed step.
     * @param dt Fixed substep seconds.
     * @param decayRate Trail exponential decay rate.
     * @param halfLife Freshness half-life seconds.
     * @param diffusion Diffusion coefficient.
     * @param deposits World positions that deposit trail this step.
     * @param depositStrength Per-deposit trail add.
     */
    void stepLife(float dt, float decayRate, float halfLife, float diffusion,
                  const std::vector<glm::vec3>& deposits, float depositStrength);

private:
    int index(int x, int z) const;
    bool inBounds(int x, int z) const;
    glm::vec4 sampleLifeTexel(float u, float v) const;
};

}  // namespace eve::gpuagents
