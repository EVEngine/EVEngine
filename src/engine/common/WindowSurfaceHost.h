#pragma once

// Capability interface: window -> graphics binding.
//
// The window module owns the native window lifecycle (SDL_CreateWindow /
// SDL_DestroyWindow / resize events) and the graphics module owns the render
// surface. Window must not include graphics headers — that is an upward
// dependency (window is L1, graphics is L3) and it welded the two modules
// together at link time.
//
// Instead, graphics registers itself as the window's surface host through
// eve::cap::provide<IWindowSurfaceHost>() (its Graphics base does this in the
// constructor). The window queries the capability at the moment it needs a
// surface: after native window creation, on resize, and just before the native
// window is destroyed. With no graphics module in the build, the query returns
// nullptr and window creation still works headless.
//
// The method set mirrors graphics::Graphics exactly, so the concrete backend
// (VulkanGraphics / WebGpuGraphics) implements this interface with the virtuals
// it already has; no forwarding shims are needed.

#include "common/Export.h"

namespace eve {

/** @brief Render-surface host for the window module (provided by graphics). */
class EVENGINE_API IWindowSurfaceHost {
public:
    static constexpr const char* capabilityName = "IWindowSurfaceHost";

    virtual ~IWindowSurfaceHost() = default;

    /** @brief Bind the render surface to a freshly created native window. */
    virtual void initWithWindow(void* nativeWindow) = 0;

    /** @brief Logical and pixel viewport after creation or resize. */
    virtual void setViewportSize(int logicalWidth, int logicalHeight,
                                 int pixelWidth, int pixelHeight) = 0;

    /** @brief Native window is about to be destroyed; drop the surface/swapchain. */
    virtual void onNativeWindowDestroyed() = 0;

    /** @brief Pause/resume presenting (mobile background/foreground). */
    virtual void setActive(bool active) = 0;

    /** @brief Rebuild the render surface on the next frame (mobile foreground). */
    virtual void requestSurfaceRecreate() = 0;
};

}  // namespace eve
