#pragma once
#include "common/Export.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>

namespace eve::graphics {

class Graphics;
class Shader;
class Texture;

/**
 * @brief Screen-space contact shadows for the primary directional light.
 *
 * Ray-marches a short distance along the light direction through the GBuffer
 * hardware depth attachment and darkens the already-lit scene with a black
 * alpha overlay (same blending contract as AmbientOcclusion overlays).
 *
 * @thread Render thread only.
 */
class EVENGINE_API_BACKENDS ContactShadow {
public:
    /** @brief Owns post shaders created from the borrowed Graphics factory. */
    explicit ContactShadow(Graphics* gfx);
    /** @brief Releases owned shaders. */
    ~ContactShadow();

    ContactShadow(const ContactShadow&)            = delete;
    ContactShadow& operator=(const ContactShadow&) = delete;

    /** @brief "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string& quality);
    /** @brief Returns the quality. */
    std::string getQuality() const { return quality_; }

    /**
     * @brief Camera for depth reconstruction (RH + ZO).
     * Builds inv(viewProj) and near/far used by apply*.
     */
    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ, float upX,
                   float upY, float upZ, float fovYDeg, float aspect, float nearZ, float farZ);

    /** @brief Sets the inv view proj. */
    void setInvViewProj(const glm::mat4& invViewProj);

    /**
     * @brief World-space direction toward the light (normalized on write).
     * Matches Light3D directional convention (points to the light).
     */
    void setLightDirection(float x, float y, float z);

    /** @brief Sets the ray length in world units. */
    void setLength(float length);
    /** @brief Sets the contact thickness in view-space meters. */
    void setThickness(float thickness);
    /** @brief Sets the darkening intensity in [0, +inf). */
    void setIntensity(float intensity);
    /** @brief Sets the start bias along normal/light in world units. */
    void setStartBias(float bias);

    /** @brief Returns the ray length. */
    float getLength() const { return length_; }
    /** @brief Returns the thickness. */
    float getThickness() const { return thickness_; }
    /** @brief Returns the intensity. */
    float getIntensity() const { return intensity_; }
    /** @brief Returns the start bias. */
    float getStartBias() const { return startBias_; }
    /** @brief Returns the step count for the active quality. */
    int getStepCount() const { return stepCount_; }

    /**
     * @brief Darken the current target using contact shadows from GBuffer depth.
     * @param hwDepth Hardware D32 depth (.r = Vulkan NDC z) or WebGPU depth color.
     * @param worldNormal Optional GBuffer normal (RGB = n*0.5+0.5); may be null.
     */
    void applyFromGBuffer(Graphics* gfx, Texture* hwDepth, Texture* worldNormal);

    /** @brief Returns the owned contact-shadow shader.
     * @lifetime Returned shader is borrowed and owned by Graphics. */
    Shader* getShader() const { return shader_; }

private:
    void applyQualityDefaults();
    void uploadCommon(int width, int height);

    Graphics*   gfx_    = nullptr;
    Shader*     shader_ = nullptr;
    glm::mat4   invViewProj_{1.f};
    glm::vec3   lightDir_{0.48f, 0.86f, 0.28f};
    std::string quality_   = "medium";
    float       nearZ_     = 0.1f;
    float       farZ_      = 100.f;
    float       length_    = 0.35f;
    float       thickness_ = 0.12f;
    float       intensity_ = 0.65f;
    float       startBias_ = 0.01f;
    int         stepCount_ = 12;
};

}  // namespace eve::graphics
