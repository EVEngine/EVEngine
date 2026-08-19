#pragma once

#include <string>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Screen-space single-bounce GI (SSGI).
 *
 * Samples scene color (RGB = lit radiance) plus hardware D32 depth, and adds
 * bounced light from nearby occluders. Manual fullscreen overlay
 * (`applyFromScene` / `applyFromDepth`). The 3D default path does not
 * auto-apply it: sampling lit scene color reprints nearby props onto the
 * floor as swimming ghosts. Mesh shaders still add hemispheric sky/ground
 * + wrap fill. Tests may still pass a packed RGBA8 (RGB = albedo, A =
 * linear depth) via applyFromDepth.
 *
 * Quality presets ("low" | "medium" | "high") tune sample count and radius.
 */
class GlobalIllumination {
public:
    explicit GlobalIllumination(Graphics *gfx);
    ~GlobalIllumination();

    GlobalIllumination(const GlobalIllumination &) = delete;
    GlobalIllumination &operator=(const GlobalIllumination &) = delete;

    /** @brief "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string &quality);
    std::string getQuality() const { return quality_; }

    /**
     * @brief Camera for depth reconstruction (RH + ZO).
     * Builds inv(viewProj) and near/far used by applyFromDepth.
     */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ,
                   float upX, float upY, float upZ, float fovYDeg, float aspect, float nearZ,
                   float farZ);

    void setInvViewProj(const glm::mat4 &invViewProj);

    void setRadius(float radius);
    void setIntensity(float intensity);
    void setLightDirection(float dx, float dy, float dz);
    void setLightColor(float r, float g, float b);

    float getRadius() const { return radius_; }
    float getIntensity() const { return intensity_; }

    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    int getSampleCount() const;

    /**
     * @brief Overlay bounced light onto the currently bound canvas / screen.
     * Packed path (tests): RGB=albedo/lit, A=linear depth 0..1.
     * 3D path: applyFromScene(color, hwDepth) with D32 NDC z.
     */
    void applyFromDepth(Graphics *gfx, Texture *packedAlbedo);
    void applyFromDepthTo(Graphics *gfx, Texture *packedAlbedo, Canvas *dest);
    void applyFromScene(Graphics *gfx, Texture *color, Texture *hwDepth);

    Shader *getShader() const { return ssgi_; }

private:
    void applyQualityDefaults();
    void uploadUniforms(int width, int height);
    void drawFullscreen(Graphics *gfx, Texture *source, Shader *shader, Texture *hwDepth = nullptr);

    Graphics *gfx_ = nullptr;
    Shader *ssgi_ = nullptr;
    std::string quality_ = "medium";
    glm::mat4 invViewProj_{1.f};
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    float radius_ = 1.25f;
    float intensity_ = 0.45f;
    glm::vec3 lightDir_{0.4f, 1.f, 0.3f};
    glm::vec3 lightColor_{1.f, 1.f, 1.f};
};

}  // namespace eve::graphics
