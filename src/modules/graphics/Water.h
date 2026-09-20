#pragma once

#include "graphics/Shader.h"
#include "graphics/WaterStyleConfig.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace eve::graphics {

/** @brief Pcg procedural water mesh shape. Custom meshes remain caller-supplied Mesh resources. */
enum class WaterMeshType : std::uint8_t { Plane = 0, Circle = 1 };

/** @brief Authored dimensions and density for Pcg-compatible procedural water geometry. */
struct WaterMeshSettings {
    float sizeX = 200.0F;
    float sizeY = 30.0F;
    float sizeZ = 200.0F;
    float densityX = 50.0F;
    float densityY = 50.0F;
    float height = 0.0F;
    WaterMeshType type = WaterMeshType::Plane;
};

/** @brief Calculate Pcg PWS_WaterSystem's triangle count without allocating a mesh. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<int> calculateWaterMeshTriangles(const WaterMeshSettings& settings);

class Graphics;
class Mesh;
class Texture;

/** @brief One linear RGB stop in a Pcg-compatible water depth gradient. */
struct WaterGradientColorStop { float time = 0.0F; glm::vec3 color{0.0F}; };
/** @brief One linear alpha stop in a Pcg-compatible water depth gradient. */
struct WaterGradientAlphaStop { float time = 0.0F; float alpha = 1.0F; };

/** @brief Caller-owned color and alpha keys used to bake a water depth-ramp texture. */
struct EVENGINE_API_BACKENDS WaterDepthGradient {
    std::vector<WaterGradientColorStop> colorStops;
    std::vector<WaterGradientAlphaStop> alphaStops;
    /** @brief Add a finite normalized RGB stop, preserving no external reference. */
    [[nodiscard]] Result<void> addColorStop(float time, float red, float green, float blue);
    /** @brief Add a finite normalized alpha stop, preserving no external reference. */
    [[nodiscard]] Result<void> addAlphaStop(float time, float alpha);
    /** @brief Remove all owned color and alpha stops. */
    void clear();
};

/**
 * @brief Dynamic water surface with sky reflection and animated ripples.
 *
 * A custom Mesh3D fragment shader renders a flat plane as water:
 *   - Reflects the sky via the environment cubemap (binding 3), Fresnel-weighted
 *     so grazing angles reflect the sky more strongly.
 *   - Ripples near the water's edge (waves lapping the shore), strongest at the
 *     boundary and fading inward.
 *   - Occasional expanding ripple rings in the middle (rain drops / fish / boat),
 *     appearing at staggered intervals.
 *   - A sun glint highlight from the primary directional light.
 *
 * Parameters are packed into the shader push-constant block (data[0..31]); see
 * Water::bindDefaults for the layout. Caller owns Water*; its Mesh / Shader are
 * owned by Graphics.
 */
class EVENGINE_API_BACKENDS Water {
public:
    explicit Water(Graphics *gfx);
    ~Water();

    Water(const Water &) = delete;
    Water &operator=(const Water &) = delete;

    /** @brief Build a flat XZ plane (Y-up) sized sizeX × sizeZ with UVs in [0,1]². */
    void createPlane(float sizeX, float sizeZ, int segX, int segZ);

    /**
     * @brief Validate, generate and atomically publish a Pcg-compatible plane or concentric-circle mesh.
     * @param settings Borrowed immutable dimensions, height, density and shape.
     * @return Published vertex count. Failure preserves the current mesh.
     * @thread Graphics owner thread only. No callback or borrowed pointer survives the call.
     */
    [[nodiscard]] Result<int> createProceduralMesh(const WaterMeshSettings& settings);

    /** @brief Advance the animation clock by dt seconds. */
    void update(float dt);
    void setTime(float seconds);
    float getTime() const { return time_; }

    // --- Animation / material knobs ---
    void setWaveSpeed(float speed);
    float getWaveSpeed() const { return config_.waveSpeed; }

    /** @brief Amplitude of the shore-edge waves. */
    void setWaveAmplitude(float amp);
    float getWaveAmplitude() const { return config_.waveAmplitude; }

    /** @brief Amplitude of the occasional middle drop ripples. */
    void setRippleAmplitude(float amp);
    float getRippleAmplitude() const { return config_.rippleAmplitude; }

    /** @brief Compatibility setter for the world-space shoreline foam width. */
    void setEdgeFalloff(float edge);
    float getEdgeFalloff() const { return config_.foamWidth; }

    /** @brief How many expanding drop ripples exist. */
    void setRippleCount(int count);
    int getRippleCount() const { return config_.rippleCount; }

    /** @brief Seconds between drop ripples. */
    void setRippleInterval(float seconds);
    float getRippleInterval() const { return config_.rippleInterval; }

    void setWaveScale(float scale);
    float getWaveScale() const { return config_.waveScale; }

    /** @brief Set Pcg PWS_WaterSystem wave direction in finite degrees around world Y. */
    [[nodiscard]] Result<void> setWaveDirectionAngle(float degrees);
    /** @brief Return the normalized wave direction angle in [0,360). */
    float getWaveDirectionAngle() const { return waveDirectionAngle_; }

    /**
     * @brief Bake Pcg GenerateColorDepth semantics and atomically publish the clamp-sampled texture.
     * @param gradient Borrowed immutable color and alpha keys.
     * @param resolution Square texture resolution in [2,4096]; columns sample x/resolution.
     * @return Generated pixel count. Failure preserves the current ramp.
     * @thread Graphics owner thread only; no pointer or callback survives the call.
     */
    [[nodiscard]] Result<int> setDepthGradient(const WaterDepthGradient& gradient, int resolution);
    /** @brief Return the Graphics-owned current depth-ramp texture, or null when disabled. */
    Texture* getDepthGradientTexture() const { return depthGradientTexture_; }
    /** @brief Disable depth-ramp sampling without destroying the Graphics-owned texture. */
    void clearDepthGradient() { depthGradientTexture_ = nullptr; }

    void setWaterColor(float r, float g, float b);
    void setReflectionTint(float r, float g, float b);
    void setReflectionIntensity(float intensity);
    float getReflectionIntensity() const { return config_.reflectionIntensity; }

    void setSunIntensity(float intensity);
    float getSunIntensity() const { return config_.sunIntensity; }

    /**
     * @brief Optional screen-space reflection overlay. When enabled, the shader
     * samples the temporally resolved SSR-chain result (bound through mesh
     * binding 6) at the fragment's screen UV and blends it over the environment
     * cubemap backup. The drawable viewport is inferred when setViewport was
     * not called. SSR must also be enabled on RenderControl.
     */
    void setScreenSpaceReflection(bool enabled, float strength = 0.85f);
    bool getScreenSpaceReflection() const { return config_.screenSpaceReflection; }
    float getScreenSpaceReflectionStrength() const { return config_.screenSpaceReflectionStrength; }

    /**
     * @brief Atomically replace all authored parameters from `eve.graphics.stylized-water/1` JSON.
     * @param json Borrowed UTF-8 JSON used only for this call.
     * @return Success after validation and publication; failure preserves the previous configuration.
     * @thread Main/render thread only. Does not invoke callbacks or allocate GPU resources.
     */
    [[nodiscard]] Result<void> applyConfigJson(const std::string& json);
    /** @brief Return the canonical current `eve.graphics.stylized-water/1` JSON snapshot. */
    [[nodiscard]] Result<std::string> configJson() const;
    /** @brief Return the immutable current configuration snapshot by value. */
    WaterStyleConfig config() const { return config_; }

    /** @brief Window / target size in pixels, used to compute screen-space UVs. */
    void setViewport(float width, float height);
    float getViewportWidth() const { return viewportW_; }
    float getViewportHeight() const { return viewportH_; }

    /** @brief Upload current params to the shader push constants. */
    void bindParams();

    /** @brief Draw the water plane (uses default mesh3d camera / lighting state). */
    void draw();

    /**
     * @brief Draw with an optional caller-rendered planar reflection.
     * @param planarReflection Borrowed texture sampled only during this synchronous draw; may be null.
     *        The caller retains ownership and may release it after this call returns.
     * @param strength Reflection contribution in [0,4]. A supplied planar reflection takes priority
     *        over the configured screen-space reflection history for this draw only.
     * @thread Main/render thread only. The call does not retain pointers or invoke callbacks.
     */
    void drawWithPlanarReflection(Texture* planarReflection, float strength = 1.0F);

    /** @brief Enable or disable inclusion in reflection-probe captures. */
    void setReflectionCaptureEnabled(bool enabled) { reflectionCaptureEnabled_ = enabled; }
    /** @brief Return whether this water surface is included in reflection-probe captures. */
    bool getReflectionCaptureEnabled() const { return reflectionCaptureEnabled_; }
    /** @brief Set the reflection-capture visibility layer mask. */
    void setReflectionCaptureMask(uint32_t mask) { reflectionCaptureMask_ = mask; }
    /** @brief Return the reflection-capture visibility layer mask. */
    uint32_t getReflectionCaptureMask() const { return reflectionCaptureMask_; }

    Shader *getShader() const { return shader_; }
    Mesh *getMesh() const { return mesh_; }

    /** @brief Names of the push-constant parameters (for UI / inspection). */
    static int paramCount();
    static std::string paramName(int index);

private:
    void drawWithReflection(Texture* reflection, float strength);
    void drawReflectionCapture();

    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    Mesh *mesh_ = nullptr;

    float time_ = 0.f;
    float waveDirectionAngle_ = 0.0F;
    Texture* depthGradientTexture_ = nullptr;
    WaterStyleConfig config_;
    float viewportW_ = 0.f;
    float viewportH_ = 0.f;
    uint64_t captureDrawerToken_ = 0;
    uint32_t reflectionCaptureMask_ = 0xffffffffu;
    bool reflectionCaptureEnabled_ = true;
};

/** @brief Create the embedded water fragment shader (owned by Graphics). */
Shader *newWaterShader(Graphics *gfx);

}  // namespace eve::graphics
