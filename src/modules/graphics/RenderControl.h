#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "graphics/GBuffer.h"
#include "graphics/LightingMode.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

class Graphics;

/**
 * @brief Declarative, compilable 3D render control.
 *
 * Callers enable/disable string features, then compile() into an ordered pass
 * list consumed by RenderSystem3D. Unknown feature names are ignored (supports()
 * reports false).
 *
 * Built-in features:
 *   "depthTest"     — HW z-buffer on forward/gbuffer draws (default on)
 *   "shadow"        — directional CSM shadow pass (default on)
 *   "gbuffer"       — fill sampleable depth + normal buffers (on when ao/gi)
 *   "gbufferAlbedo" — also write albedo into GBuffer (implies gbuffer; on when gi)
 *   "forward"       — lit forward / clustered mesh draws (default on)
 *   "hair"          — transparent hair pass after opaque (default on)
 *   "clustered"     — prefer clustered forward when light count > 8 (default on)
 *   "clusteredDeferred" — projection of LightingMode::Hybrid (enum is the source of truth)
 *   "ao"            — screen-space AO overlay after FXAA resolve (implies gbuffer; default on)
 *   "outline"       — screen-space model outline from depth+normal (implies gbuffer; default off)
 *   "frustumCull"   — conservative bounding-sphere frustum culling during the job-ified
 *                     frame data prep (default off; meshes without bounds are never culled)
 *   "gi"            — enables gbufferAlbedo; mesh hemispheric GI (fullscreen SSGI is not auto-applied)
 *   "rtgi"          — fullscreen real-time GI post pass (SSGI) overlay
 *   "aa"            — FXAA resolve of the 3D scene color into the swapchain (default on)
 *   "taa"           — full temporal AA with projection jitter, motion/depth reprojection,
 *                     Catmull-Rom history, YCoCg variance clipping and adaptive sharpening;
 *                     enabling it disables MSAA by default
 *   "msaa"          — hardware MSAA on the 3D scene color pass (default on; sample count via
 *                     Graphics.setMsaaSamples, default 4, clamped to device support)
 *   "ssr"           — PBR-aware screen-space reflections with coarse/fine tracing and temporal denoise
 *   "reflectionChain"— enables TAA plus the complete RTGI then SSR lighting chain (default off)
 *   "decal"         — screen-space decal layer pass between gbuffer and forward
 *                     (implies gbuffer; default off)
 *   "atmosphere"    — analytic sky/aerial-perspective pass (default on)
 *   "volumetricFog" — froxel media, lighting, integration and composite passes
 *   "fogLocalVolumes" — local volume injection (implies volumetricFog)
 *   "fogTemporal"   — history filtering (implies volumetricFog)
 *
 * Lighting mode (enum is authoritative; "clusteredDeferred" is a projection):
 *   LightingMode::ForwardPlus — today's clustered-forward opaque path (default)
 *   LightingMode::Hybrid — opaque clustered deferred + transparent Forward+ when the
 *     deferredLighting pass is available; otherwise compile() falls back observably
 *     to ForwardPlus (getEffectiveLightingMode() / didFallbackFromHybridLighting())
 *
 * 3D draws into a sampleable scene color target (not the swapchain). Present
 * resolves that target (FXAA when "aa" is on), then composites AO/HUD.
 *
 * compile() is idempotent; enable/disable mark the control dirty until the
 * next compile(). RenderSystem3D auto-compiles when dirty.
 */
class EVENGINE_API_BACKENDS RenderControl {
public:
    RenderControl();
    ~RenderControl() = default;

    RenderControl(const RenderControl &) = delete;
    RenderControl &operator=(const RenderControl &) = delete;

    void attach(Graphics *gfx);
    Graphics *getGraphics() const { return gfx_; }

    bool supports(const std::string &feature) const;
    void enable(const std::string &feature);
    void disable(const std::string &feature);
    bool isEnabled(const std::string &feature) const;

    /**
     * @brief Request the opaque lighting strategy (transparent stays Forward+).
     * @param mode ForwardPlus or Hybrid.
     * @return Success. Hybrid may still fall back at compile() until deferred lighting ships.
     * @thread Render-thread affine; no callbacks.
     */
    [[nodiscard]] Result<void> setLightingMode(LightingMode mode);
    /**
     * @brief Request lighting mode by name: "forwardPlus" / "forward+" / "hybrid".
     * @return Success, or Unsupported for unknown names (previous mode unchanged).
     */
    [[nodiscard]] Result<void> setLightingMode(const std::string &name);
    /** @brief Return the caller-requested lighting mode (may differ from effective). */
    LightingMode getLightingMode() const { return lightingMode_; }
    /**
     * @brief Return the mode actually compiled into the pass list.
     * @lifetime Valid after a successful compile(); equals ForwardPlus while dirty.
     */
    LightingMode getEffectiveLightingMode() const { return effectiveLightingMode_; }
    /**
     * @brief True when deferredLighting can be inserted for Hybrid.
     * Phase A returns false until the lighting pass is implemented.
     */
    bool isDeferredLightingAvailable() const;
    /**
     * @brief True after compile() when Hybrid was requested but fell back to ForwardPlus.
     * Cleared on the next compile that does not need a fallback.
     */
    bool didFallbackFromHybridLighting() const { return hybridLightingFallback_; }

    /**
     * @brief Set the shared TAA/RTGI/SSR quality preset used by the automatic reflection chain.
     * @param quality One of "low", "medium", "high" or "ultra"; unknown values use "high".
     */
    void setReflectionQuality(const std::string &quality);
    /** @brief Return the shared TAA/RTGI/SSR quality preset. */
    std::string getReflectionQuality() const { return reflectionQuality_; }
    /** @brief Set the unified post-process quality for TAA, RTGI and SSR. */
    void setPostProcessQuality(const std::string &quality) { setReflectionQuality(quality); }
    /** @brief Return the unified post-process quality for TAA, RTGI and SSR. */
    std::string getPostProcessQuality() const { return reflectionQuality_; }

    /** @brief Rebuild the executable pass list from current feature flags. */
    void compile();
    bool isCompiled() const { return compiled_ && !dirty_; }
    bool isDirty() const { return dirty_; }

    int getPassCount() const { return int(passes_.size()); }
    /** @brief Return the compiled pass name at index, or an empty string. */
    std::string getPassName(int index) const;
    bool hasPass(const std::string &name) const;

    GBuffer *getGBuffer() { return &gbuffer_; }
    const GBuffer *getGBuffer() const { return &gbuffer_; }

    /** @brief Ensure compiled; no-op when already clean. */
    void ensureCompiled();

private:
    void setFeature(const std::string &feature, bool enabled);
    void syncClusteredDeferredFeature();

    Graphics *gfx_ = nullptr;
    std::unordered_map<std::string, bool> features_;
    std::vector<std::string> passes_;
    std::string reflectionQuality_ = "high";
    GBuffer gbuffer_;
    LightingMode lightingMode_          = LightingMode::ForwardPlus;
    LightingMode effectiveLightingMode_ = LightingMode::ForwardPlus;
    bool hybridLightingFallback_        = false;
    bool dirty_                         = true;
    bool compiled_                      = false;
};

}  // namespace eve::graphics
