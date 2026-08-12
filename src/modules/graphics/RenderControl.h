#pragma once

#include "graphics/GBuffer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

class Graphics;

/**
 * Declarative, compilable 3D render control.
 *
 * Callers enable/disable string features, then compile() into an ordered pass
 * list consumed by RenderSystem3D. Unknown feature names are ignored (supports()
 * reports false).
 *
 * Built-in features:
 *   "depthTest"     — HW z-buffer on forward/gbuffer draws (default on)
 *   "shadow"        — directional CSM shadow pass (default on)
 *   "gbuffer"       — fill sampleable depth + normal buffers (default off)
 *   "gbufferAlbedo" — also write albedo into GBuffer (implies gbuffer)
 *   "forward"       — lit forward / clustered mesh draws (default on)
 *   "hair"          — transparent hair pass after opaque (default on)
 *   "clustered"     — prefer clustered forward when light count > 8 (default on)
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

    /** Rebuild the executable pass list from current feature flags. */
    void compile();
    bool isCompiled() const { return compiled_ && !dirty_; }
    bool isDirty() const { return dirty_; }

    int getPassCount() const { return int(passes_.size()); }
    /** Pass names: "shadow" | "gbuffer" | "forward" | "hair" */
    std::string getPassName(int index) const;
    bool hasPass(const std::string &name) const;

    GBuffer *getGBuffer() { return &gbuffer_; }
    const GBuffer *getGBuffer() const { return &gbuffer_; }

    /** Ensure compiled; no-op when already clean. */
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
