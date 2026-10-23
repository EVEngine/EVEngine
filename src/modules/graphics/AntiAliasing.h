#pragma once
#include "common/Export.h"


#include <cstdint>
#include <string>
#include <unordered_map>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Classic image-space anti-aliasing.
 *
 * Modes:
 *  - "fxaa" — FXAA 3.11-style luminance edge search + subpixel blend
 *  - "taa" — temporal-style alias that blends with previous-frame history
 *  - "smaa" — SMAA-inspired single-pass morphological AA (fits 1×MainTex post path)
 *  - "ssaa" — supersample resolve (box / tent / Gaussian); render into a larger Canvas first
 *  - "nfaa" — Normal Filter AA (blur along luma-gradient tangent)
 *
 * Quality presets ("low" | "medium" | "high") tune thresholds / search / kernel radius.
 * Workflow: render scene → Canvas → aa.apply / aa.applyTo → screen or next post.
 */
class EVENGINE_API_BACKENDS AntiAliasing {
public:
    /** @brief Anti aliasing. */
    explicit AntiAliasing(Graphics *gfx);
    /** @brief Anti aliasing. */
    ~AntiAliasing();

    AntiAliasing(const AntiAliasing &) = delete;
    AntiAliasing &operator=(const AntiAliasing &) = delete;

    /** @brief "low" | "medium" | "high" (unknown → medium). */
    void setQuality(const std::string &quality);
    /** @brief Returns the quality. */
    std::string getQuality() const { return quality_; }

    /** @brief "fxaa" | "smaa" | "ssaa" | "nfaa" | "taa" (unknown → fxaa). */
    void setMode(const std::string &mode);
    /** @brief Returns the mode. */
    std::string getMode() const { return mode_; }

    /** @brief True when param. */
    bool hasParam(const std::string &name) const;
    /** @brief Sets the float. */
    void setFloat(const std::string &name, float value);
    /** @brief Returns the float. */
    float getFloat(const std::string &name) const;

    /**
     * @brief Suggested supersample scale for the active quality when using "ssaa"
     * (2 for low/medium, 4 for high). Callers create the source Canvas at
     * destSize * suggestScale().
     */
    float suggestScale() const;

    /** Source pixel count helper: floor(dest * suggestScale()), at least 1. */
    /** @brief Resolution for. */
    int resolutionFor(int destSize) const;

    /**
     * @brief Apply current mode to `source`, writing into the currently bound canvas / screen.
     * Uploads texelW/texelH from the source size.
     */
    void apply(Graphics *gfx, Texture *source);
    /** @brief Resolve TAA history and composite it into an arbitrary destination rectangle. */
    void applyTemporalRect(Graphics *gfx, Texture *source, float x, float y, float width,
                           float height, float r = 1.f, float g = 1.f, float b = 1.f,
                           float a = 1.f);
    /** @brief Applies temporal. */
    void applyTemporal(Graphics *gfx, Texture *source, Texture *motion = nullptr);
    /**
     * @brief Resolve TAA into history and return the linear HDR result without compositing it.
     * @lifetime Returned texture is borrowed from this resolver until temporal targets are recreated.
     */
    Texture *resolveTemporal(Graphics *gfx, Texture *source, Texture *motion = nullptr);

    /**
     * @brief Prepare uniforms/resources for a backend-recorded temporal pass.
     * @lifetime Returned canvas is borrowed from this resolver until temporal targets are recreated.
     */
    Canvas *beginTemporalFrame(Texture *source, Texture *motion = nullptr);
    /**
     * @brief Previous resolved frame, or nullptr when history is invalid.
     * @lifetime Returned texture is borrowed from this resolver until temporal targets are recreated.
     */
    Texture *getTemporalReadTexture() const;
    /** @brief Mark the prepared write target valid and rotate temporal buffers. */
    void endTemporalFrame();
    /** @brief Return this frame's Halton projection jitter in NDC units. */
    glm::vec2 prepareTemporalJitter(int width, int height);
    /** @brief Detect camera cuts and invalidate incompatible temporal history. */
    void setTemporalCamera(const glm::vec3 &eye, const glm::vec3 &target, float fovYDeg);
    /** @brief Discard TAA history after a render-chain discontinuity. */
    void invalidateTemporalHistory();
    /** @brief Set the jittered camera matrix used for depth-based history reprojection. */
    void setTemporalViewProjection(const glm::mat4 &viewProjection, float nearZ, float farZ);
    /** @brief Per-object UV correction relative to static-world camera reprojection. */
    glm::vec2 prepareTemporalObjectMotion(const void *objectKey, const glm::mat4 &model);
    /** @brief Applies to. */
    void applyTo(Graphics *gfx, Texture *source, Canvas *dest);

    /** @brief Applies canvas. */
    void applyCanvas(Graphics *gfx, Canvas *source);
    /** @brief Applies canvas to. */
    void applyCanvasTo(Graphics *gfx, Canvas *source, Canvas *dest);

    /** @brief Upload texel uniforms from `source` without drawing (3D scene-color resolve). */
    void prepareSource(Texture *source);

    /** @lifetime Shader getters return Graphics-owned borrowed shaders. */
    /** @brief Returns the fxaa shader. */
    Shader *getFxaaShader() const { return fxaa_; }
    /** @brief Returns the smaa shader. */
    Shader *getSmaaShader() const { return smaa_; }
    /** @brief Returns the ssaa shader. */
    Shader *getSsaaShader() const { return ssaa_; }
    /** @brief Returns the nfaa shader. */
    Shader *getNfaaShader() const { return nfaa_; }
    /** @brief Returns the taa shader. */
    Shader *getTaaShader() const { return taa_; }
    /** @brief Returns the shader. */
    Shader *getShader() const;

private:
    void applyQualityDefaults();
    void uploadScreenUniforms(Texture *source);
    void drawFullscreen(Graphics *gfx, Texture *source, Shader *shader);
    void applyFullscreenTo(Graphics *gfx, Texture *source, Canvas *dest, Texture *history,
                          bool hasHistory);
    void ensureHistoryCanvases(int width, int height);
    void swapHistoryBuffers();
    void resetHistory();
    Shader *shaderForMode() const;

    Graphics *gfx_ = nullptr;  // not owned
    Shader *fxaa_ = nullptr;   // owned by Graphics
    Shader *smaa_ = nullptr;
    Shader *ssaa_ = nullptr;
    Shader *nfaa_ = nullptr;
    Shader *taa_ = nullptr;
    std::string quality_ = "medium";
    std::string mode_ = "fxaa";

    Canvas *taaHistoryA_ = nullptr;
    Canvas *taaHistoryB_ = nullptr;
    Canvas *historyRead_ = nullptr;
    Canvas *historyWrite_ = nullptr;
    bool taaHistoryValid_ = false;
    int historyW_ = 0;
    int historyH_ = 0;
    uint32_t temporalFrameIndex_ = 0;
    glm::vec2 temporalJitterNdc_{0.f};
    glm::vec2 previousJitterNdc_{0.f};
    glm::vec3 temporalEye_{0.f};
    glm::vec3 temporalForward_{0.f, 0.f, -1.f};
    float temporalFovY_ = 0.f;
    bool temporalCameraValid_ = false;
    glm::mat4 temporalViewProj_{1.f};
    glm::mat4 previousViewProj_{1.f};
    float temporalNearZ_ = 0.1f;
    float temporalFarZ_ = 100.f;
    bool temporalViewValid_ = false;
    std::unordered_map<const void *, glm::mat4> temporalObjectHistory_;
    std::unordered_map<const void *, glm::mat4> temporalObjectPending_;
};

}  // namespace eve::graphics
