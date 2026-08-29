#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {

class Camera3D;
class Canvas;
class Graphics;
class Mesh;
class ReflectionProbeRegistry;
class Texture;

/**
 * @brief Incremental six-face HDR scene capture for a runtime reflection probe.
 *
 * Captured faces remain linear RGBA16F Canvas resources. Publishing them as a
 * filtered cubemap is a separate backend operation, so capture completion never
 * exposes an unfiltered or partially updated probe to lighting.
 */
class ReflectionProbeCapture {
public:
    explicit ReflectionProbeCapture(Graphics *gfx);
    /** @brief Detach this probe from every registry before script-owned destruction. */
    ~ReflectionProbeCapture();

    /**
     * @brief Configure the probe origin, face resolution and clipping planes.
     * @param x Probe world-space X position.
     * @param y Probe world-space Y position.
     * @param z Probe world-space Z position.
     * @param resolution Square face resolution, clamped to [16, 2048].
     * @param nearZ Near clipping distance.
     * @param farZ Far clipping distance.
     */
    void configure(float x, float y, float z, int resolution = 128, float nearZ = 0.1f,
                   float farZ = 100.f);

    /** @brief Mark all six faces dirty and restart capture at +X. */
    void requestCapture();
    /**
     * @brief Queue a coherent recapture without restarting a revision already in progress.
     *
     * If no face has been submitted yet the current input snapshot is refreshed immediately.
     */
    void queueCapture();

    /**
     * @brief Capture up to @p faceBudget dirty faces.
     * @return True when all six HDR faces are complete.
     */
    bool update(int faceBudget = 1);

    /** @brief Return one captured HDR face Canvas in +X,-X,+Y,-Y,+Z,-Z order. */
    Canvas *getFaceCanvas(int face) const;
    /** @brief True while one or more faces still need capture. */
    bool isCapturePending() const { return pending_; }
    /** @brief True when changed capture inputs are queued behind the current revision. */
    bool isRecaptureQueued() const { return recaptureAfterPublish_; }
    /** @brief True after at least one complete six-face capture. */
    bool isCaptureComplete() const { return revision_ > 0 && !pending_; }
    /** @brief Monotonic revision incremented after each complete six-face capture. */
    uint32_t getRevision() const { return revision_; }
    /** @brief Copy a complete capture revision into a backend HDR staging cubemap. */
    bool stageCapturedFaces();
    /** @brief Backend HDR staging cubemap; not yet filtered or safe for lighting. */
    Texture *getStagingCubemap() const { return stagingCubemap_; }
    /** @brief Last capture revision fully copied into the staging cubemap. */
    uint32_t getStagedRevision() const { return stagedRevision_; }
    /** @brief Filter with 8-512 samples and atomically publish the active cubemap. */
    bool filterAndPublish(int sampleCount = 64);
    /** @brief Last fully filtered cubemap, safe to assign to Camera3D reflection probes. */
    Texture *getActiveCubemap() const { return activeCubemap_; }
    /** @brief Last capture revision atomically published for lighting. */
    uint32_t getPublishedRevision() const { return publishedRevision_; }
    /** @brief Set refresh mode: static, on_demand, time_sliced, or realtime. */
    void setUpdateMode(const std::string &mode);
    /** @brief Return the current refresh mode. */
    std::string getUpdateMode() const;
    /** @brief Set automatic refresh interval in frames for time_sliced mode. */
    void setRefreshInterval(int frames);
    /** @brief Return the automatic refresh interval in frames. */
    int getRefreshInterval() const { return refreshInterval_; }
    /** @brief Set the object-layer mask visible to this reflection capture. */
    void setCaptureMask(int mask);
    /** @brief Return the object-layer mask visible to this reflection capture. */
    int getCaptureMask() const { return static_cast<int>(captureMask_); }
    /**
     * @brief Set non-recursive global environment lighting for captured geometry.
     *
     * This affects material IBL but does not draw a sky background into empty pixels.
     * Local reflection probes remain disabled on the capture camera.
     */
    void setEnvironmentLighting(Texture *environment, float intensity = 1.f);
    /** @brief Return the global environment texture used while capturing. */
    Texture *getEnvironmentLighting() const { return environmentLighting_; }
    /** @brief Return the global environment-lighting intensity used while capturing. */
    float getEnvironmentLightingIntensity() const { return environmentLightingIntensity_; }
    /**
     * @brief Set a linear HDR backup sky color for empty capture pixels.
     * @param r Linear red channel before intensity scaling.
     * @param g Linear green channel before intensity scaling.
     * @param b Linear blue channel before intensity scaling.
     * @param intensity Non-negative HDR multiplier.
     */
    void setSkyColor(float r, float g, float b, float intensity = 1.f);
    /**
     * @brief Override one directional backup sky face in +X,-X,+Y,-Y,+Z,-Z order.
     * @param face Face index in [0,5].
     * @param r Linear red channel.
     * @param g Linear green channel.
     * @param b Linear blue channel.
     */
    void setSkyFaceColor(int face, float r, float g, float b);
    /**
     * @brief Set a directional per-pixel sky texture for one capture face.
     * @param face Face index in +X,-X,+Y,-Y,+Z,-Z order.
     * @param texture RGBA sky face matching cubemap projection, or nullptr for color backup.
     */
    void setSkyFaceTexture(int face, Texture *texture);
    /** @brief Return one directional per-pixel sky texture, or nullptr. */
    Texture *getSkyFaceTexture(int face) const;
    /** @brief Set the linear HDR radiance multiplier for one sky face texture. */
    void setSkyFaceTextureScale(int face, float scale);
    /** @brief Return one sky face texture's linear HDR radiance multiplier. */
    float getSkyFaceTextureScale(int face) const;
    /** @brief Return one directional sky-face color component, or zero for invalid indices. */
    float getSkyFaceColor(int face, int component) const;
    /** @brief Return the unscaled backup sky red channel. */
    float getSkyR() const { return skyR_[0]; }
    /** @brief Return the unscaled backup sky green channel. */
    float getSkyG() const { return skyG_[0]; }
    /** @brief Return the unscaled backup sky blue channel. */
    float getSkyB() const { return skyB_[0]; }
    /** @brief Return the backup sky HDR multiplier. */
    float getSkyIntensity() const { return skyIntensity_; }
    /** @brief Number of cubemap faces remaining in the current capture revision. */
    int getPendingFaceCount() const { return pending_ ? 6 - nextFace_ : 0; }
    /** @brief Number of faces rendered by the most recent update call. */
    int getLastCapturedFaceCount() const { return lastCapturedFaceCount_; }
    /** @brief Total number of face renders performed by this capture object. */
    uint32_t getTotalCapturedFaceCount() const { return totalCapturedFaceCount_; }
    /** @brief Sample count used by the most recently published filter pass. */
    int getLastFilterSampleCount() const { return lastFilterSampleCount_; }
    /** @brief Set the GPU-time budget used by adaptive capture scheduling, in milliseconds. */
    void setGpuBudgetMs(float milliseconds);
    /** @brief Return the adaptive capture GPU-time budget in milliseconds. */
    float getGpuBudgetMs() const { return gpuBudgetMs_; }
    /**
     * @brief Report a backend timestamp duration for the most recent probe workload.
     * @param milliseconds GPU duration in milliseconds; non-positive values are ignored.
     */
    void reportGpuDurationMs(float milliseconds);
    /** @brief Return the exponentially smoothed reported GPU duration. */
    float getSmoothedGpuDurationMs() const { return smoothedGpuDurationMs_; }
    /** @brief Return the current adaptive cubemap-face budget. */
    int getAdaptiveFaceBudget() const { return adaptiveFaceBudget_; }
    /** @brief Return the current adaptive filter sample count. */
    int getAdaptiveFilterSamples() const { return adaptiveFilterSamples_; }
    /**
     * @brief Advance capture and filtering using the GPU-budget controller's workload.
     * @return True only when this call publishes a new active revision.
     */
    bool tickAdaptive();
    /**
     * @brief Advance capture, staging, filtering and publishing under the selected policy.
     * @return True only when this call publishes a new active revision.
     */
    bool tick(int faceBudget = 1, int filterSamples = 64);
    /**
     * @brief Configure the persistent box influence used for lighting and editor display.
     * @param extentX Positive world-space X half extent.
     * @param extentY Positive world-space Y half extent.
     * @param extentZ Positive world-space Z half extent.
     * @param intensity Non-negative reflection intensity.
     * @param blendDistance Non-negative edge blend distance.
     * @param priority Stable overlap-selection priority.
     */
    void configureInfluence(float extentX, float extentY, float extentZ, float intensity = 1.f,
                            float blendDistance = 1.f, int priority = 0);
    /** @brief Return persistent influence half extent X. */
    float getInfluenceExtentX() const { return influenceExtentX_; }
    /** @brief Return persistent influence half extent Y. */
    float getInfluenceExtentY() const { return influenceExtentY_; }
    /** @brief Return persistent influence half extent Z. */
    float getInfluenceExtentZ() const { return influenceExtentZ_; }
    /** @brief Return persistent reflection intensity. */
    float getInfluenceIntensity() const { return influenceIntensity_; }
    /** @brief Return persistent edge blend distance. */
    float getInfluenceBlendDistance() const { return influenceBlendDistance_; }
    /** @brief Return persistent overlap-selection priority. */
    int getInfluencePriority() const { return influencePriority_; }
    /** @brief Submit the persistent influence and active cubemap to a Camera3D slot. */
    bool applyConfiguredToCamera(Camera3D *camera, int slot);
    /**
     * @brief Assign the active cubemap to a Camera3D local reflection-probe slot.
     * @return False when no filtered revision has been published yet.
     */
    bool applyToCamera(Camera3D *camera, int slot, float extentX, float extentY, float extentZ,
                       float intensity = 1.f, float blendDistance = 1.f, int priority = 0);
    /** @brief Current square face resolution. */
    int getResolution() const { return resolution_; }
    /** @brief Return probe world-space center X. */
    float getCenterX() const { return centerX_; }
    /** @brief Return probe world-space center Y. */
    float getCenterY() const { return centerY_; }
    /** @brief Return probe world-space center Z. */
    float getCenterZ() const { return centerZ_; }
    /** @brief Return the capture far distance used for spatial dirty culling. */
    float getCaptureFarDistance() const { return farZ_; }
    /**
     * @brief Set capture LOD distance scale; below one retains higher-detail LODs farther away.
     */
    void setCaptureLodDistanceScale(float scale);
    /** @brief Return capture LOD distance scale. */
    float getCaptureLodDistanceScale() const { return lodDistanceScale_; }
    /** @brief Enable or disable transparent/hair surfaces in probe captures. */
    void setCaptureTransparent(bool enabled);
    /** @brief Return whether transparent/hair surfaces are captured. */
    bool getCaptureTransparent() const { return captureTransparent_; }
    /** @brief Enable clustered lighting for captures that exceed the legacy light pack. */
    void setCaptureClusteredLighting(bool enabled);
    /** @brief Return whether probe captures can build clustered light tables. */
    bool getCaptureClusteredLighting() const { return captureClusteredLighting_; }

private:
    friend class ReflectionProbeRegistry;
    void attachRegistry(ReflectionProbeRegistry *registry);
    void detachRegistry(ReflectionProbeRegistry *registry);
    void advanceRefreshPolicy();
    void requestCaptureAfterPublish();
    void ensureResources();
    void captureFace(int face);

    Graphics *gfx_ = nullptr;
    Camera3D *camera_ = nullptr;
    std::array<Canvas *, 6> faces_{};
    Mesh *skyQuad_ = nullptr;
    float centerX_ = 0.f;
    float centerY_ = 0.f;
    float centerZ_ = 0.f;
    float nearZ_ = 0.1f;
    float farZ_ = 100.f;
    int resolution_ = 128;
    int allocatedResolution_ = 0;
    int nextFace_ = 0;
    bool pending_ = true;
    uint32_t revision_ = 0;
    Texture *stagingCubemap_ = nullptr;
    int stagingResolution_ = 0;
    uint32_t stagedRevision_ = 0;
    Texture *activeCubemap_ = nullptr;
    uint32_t publishedRevision_ = 0;
    int activeResolution_ = 0;
    enum class UpdateMode { Static, OnDemand, TimeSliced, Realtime };
    UpdateMode updateMode_ = UpdateMode::OnDemand;
    int refreshInterval_ = 60;
    uint32_t captureMask_ = 0xffffffffu;
    Texture *environmentLighting_ = nullptr;
    float environmentLightingIntensity_ = 0.f;
    std::array<float, 6> skyR_{};
    std::array<float, 6> skyG_{};
    std::array<float, 6> skyB_{};
    std::array<float, 6> captureSkyR_{};
    std::array<float, 6> captureSkyG_{};
    std::array<float, 6> captureSkyB_{};
    std::array<Texture *, 6> skyFaceTextures_{};
    std::array<Texture *, 6> captureSkyFaceTextures_{};
    std::array<float, 6> skyFaceTextureScales_{{1.f, 1.f, 1.f, 1.f, 1.f, 1.f}};
    std::array<float, 6> captureSkyFaceTextureScales_{{1.f, 1.f, 1.f, 1.f, 1.f, 1.f}};
    float skyIntensity_ = 1.f;
    float captureSkyIntensity_ = 1.f;
    Texture *captureEnvironmentLighting_ = nullptr;
    float captureEnvironmentLightingIntensity_ = 0.f;
    bool recaptureAfterPublish_ = false;
    float lodDistanceScale_ = 1.f;
    float captureLodDistanceScale_ = 1.f;
    bool captureTransparent_ = true;
    bool snapshotCaptureTransparent_ = true;
    bool captureClusteredLighting_ = true;
    bool snapshotCaptureClusteredLighting_ = true;
    int framesSincePublish_ = 0;
    int lastCapturedFaceCount_ = 0;
    uint32_t totalCapturedFaceCount_ = 0;
    int lastFilterSampleCount_ = 0;
    float gpuBudgetMs_ = 1.5f;
    float smoothedGpuDurationMs_ = 0.f;
    int adaptiveFaceBudget_ = 1;
    int adaptiveFilterSamples_ = 64;
    int underBudgetReports_ = 0;
    float influenceExtentX_ = 1.f;
    float influenceExtentY_ = 1.f;
    float influenceExtentZ_ = 1.f;
    float influenceIntensity_ = 1.f;
    float influenceBlendDistance_ = 1.f;
    int influencePriority_ = 0;
    std::vector<ReflectionProbeRegistry *> registries_;
};

}  // namespace eve::graphics
