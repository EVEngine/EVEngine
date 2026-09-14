#pragma once

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::physics {

class SoftBody3D;

/**
 * @brief Presentation-only renderer for a borrowed volumetric soft body.
 *
 * Color and mesh-cache state belong to this renderer. The simulation and
 * Graphics objects are borrowed, thread-affine, and are never retained by a
 * callback.
 */
class SoftBody3DRenderer final {
public:
    /** @brief Construct a renderer observing a nullable soft-body runtime. */
    explicit SoftBody3DRenderer(SoftBody3D* body) noexcept : body_(body) {}

    SoftBody3DRenderer(const SoftBody3DRenderer&)            = delete;
    SoftBody3DRenderer& operator=(const SoftBody3DRenderer&) = delete;

    /** @brief Replace the borrowed runtime; null disables drawing. */
    void setBody(SoftBody3D* body);
    /** @brief Return the currently borrowed runtime. */
    [[nodiscard]] SoftBody3D* getBody() const { return body_; }
    /** @brief Set the clamped RGBA presentation color. */
    void setColor(float r, float g, float b, float a);
    /** @brief Update the surface cache and draw it in the current 3D frame. */
    void draw(graphics::Graphics* graphics);

private:
    SoftBody3D*     body_            = nullptr;
    graphics::Mesh* mesh_            = nullptr;
    int             meshVertexCount_ = 0;
    int             meshIndexCount_  = 0;
    float           colorR_ = 0.92f, colorG_ = 0.42f, colorB_ = 0.24f, colorA_ = 1.f;
};

}  // namespace eve::physics
