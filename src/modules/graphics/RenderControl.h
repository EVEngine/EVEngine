#pragma once

#include "graphics/GBuffer.h"

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
 *   "ao"            — screen-space AO overlay after FXAA resolve (implies gbuffer; default on)
 *   "outline"       — screen-space model outline from depth+normal (implies gbuffer; default off)
 *   "gi"            — enables gbufferAlbedo; mesh hemispheric GI (fullscreen SSGI is not auto-applied)
 *   "aa"            — FXAA resolve of the 3D scene color into the swapchain (default on)
 *   "msaa"          — hardware MSAA on the 3D scene color pass (default on; sample count via
 *                     Graphics.setMsaaSamples, default 4, clamped to device support)
 *
 * 3D draws into a sampleable scene color target (not the swapchain). Present
 * resolves that target (FXAA when "aa" is on), then composites AO/HUD.
 *
 * compile() is idempotent; enable/disable mark the control dirty until the
 * next compile(). RenderSystem3D auto-compiles when dirty.
 */
class RenderControl {
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

    /** @brief Rebuild the executable pass list from current feature flags. */
    void compile();
    bool isCompiled() const { return compiled_ && !dirty_; }
    bool isDirty() const { return dirty_; }

    int getPassCount() const { return int(passes_.size()); }
    /** @brief Pass names: "shadow" | "gbuffer" | "forward" | "hair" */
    std::string getPassName(int index) const;
    bool hasPass(const std::string &name) const;

    GBuffer *getGBuffer() { return &gbuffer_; }
    const GBuffer *getGBuffer() const { return &gbuffer_; }

    /** @brief Ensure compiled; no-op when already clean. */
    void ensureCompiled();

private:
    void setFeature(const std::string &feature, bool enabled);

    Graphics *gfx_ = nullptr;
    std::unordered_map<std::string, bool> features_;
    std::vector<std::string> passes_;
    GBuffer gbuffer_;
    bool dirty_ = true;
    bool compiled_ = false;
};

}  // namespace eve::graphics
