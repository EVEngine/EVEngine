#pragma once

#include <string>

#include <glm/vec3.hpp>

namespace eve::graphics {

/** @brief Analytic local participating-media volume injected into atmospheric fog. */
class FogVolume {
public:
    enum class Shape { sphere, box, cylinder };

    /** @brief Set shape using "sphere", "box" or "cylinder"; unknown selects box. */
    void setShape(const std::string &shape);
    Shape getShape() const { return shape_; }
    std::string getShapeName() const;

    /** @brief Set world-space center. */
    void setPosition(float x, float y, float z) { position_ = {x, y, z}; }
    const glm::vec3 &getPosition() const { return position_; }
    /** @brief Set full world-space dimensions; values are clamped positive. */
    void setSize(float x, float y, float z);
    const glm::vec3 &getSize() const { return size_; }

    /** @brief Signed extinction; negative values carve fog but total density is clamped. */
    void setExtinction(float extinction) { extinction_ = extinction; }
    float getExtinction() const { return extinction_; }
    /** @brief Single-scattering albedo, clamped to [0,1]. */
    void setAlbedo(float r, float g, float b);
    const glm::vec3 &getAlbedo() const { return albedo_; }
    /** @brief Unlit radiance emitted per unit density. */
    void setEmissive(float r, float g, float b);
    const glm::vec3 &getEmissive() const { return emissive_; }
    /** @brief HG anisotropy in [-0.99,0.99]. */
    void setAnisotropy(float anisotropy);
    float getAnisotropy() const { return anisotropy_; }
    /** @brief Fraction of normalized radius used for a smooth edge, in [0,1]. */
    void setEdgeFalloff(float falloff);
    float getEdgeFalloff() const { return edgeFalloff_; }

    /** @brief Normalized analytic density at a world position, including signed extinction. */
    float sampleExtinction(const glm::vec3 &worldPosition) const;

private:
    Shape shape_ = Shape::box;
    glm::vec3 position_{0.f};
    glm::vec3 size_{1.f};
    float extinction_ = 0.05f;
    glm::vec3 albedo_{1.f};
    glm::vec3 emissive_{0.f};
    float anisotropy_ = 0.f;
    float edgeFalloff_ = 0.2f;
};

}  // namespace eve::graphics
