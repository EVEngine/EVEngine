#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/Module.h"

namespace eve {

namespace window {

/**
 * @brief Display settings for window creation/resize.
 * width/height == 0 selects the desktop display mode size.
 */
struct WindowSettings {
    float    refreshrate   = 0.0;
    float    dpi_scale     = 1.0;
    int32_t  x             = 0;
    int32_t  y             = 0;
    uint16_t width         = 1366;
    uint16_t height        = 768;
    uint16_t minwidth      = 1;
    uint16_t minheight     = 1;
    uint8_t  vsync         = 1;
    uint8_t  msaa          = 0;
    uint8_t  depth         = 0;
    uint8_t  display       = 0;
    bool     borderless    = false;
    bool     centered      = true;
    bool     high_dpi      = true;
    bool     use_dpi_scale = true;
    bool     use_position  = false;
    bool     fullscreen    = false;
    bool     desktop_mode  = true;
    bool     stencil       = true;
    bool     resizable     = false;
    bool     always_on_top = false;
};

/**
 * @brief Platform window interface (SDL implementation on desktop/mobile).
 * Script: `win <- eve.Window();`
 */
class Window : public Module {
public:
    Module_REG(Window);

    struct WindowSize {
        int width  = 0;
        int height = 0;

        bool operator==(const WindowSize& w) const { return w.width == width && w.height == height; }
    };

    struct MessageBoxData {
        std::string type = "info";
        std::string title;
        std::string message;
        int  enterButtonIndex  = 0;
        int  escapeButtonIndex = 0;
        bool attachToWindow    = false;
        std::vector<std::string> buttons;
    };

    virtual ~Window() {}

    /** @brief Requests a new logical window size. */
    virtual void setSize(int width, int height) = 0;
    virtual int  getWidth() const               = 0;
    virtual int  getHeight() const              = 0;

    /** @brief Applies a full WindowSettings struct (recreates the window when needed). */
    virtual bool           setWindowSettings(WindowSettings settings) = 0;
    virtual WindowSettings getWindowSettings()                        = 0;

    /** @brief Closes and destroys the underlying window. */
    virtual void close() = 0;

    /** @brief Switches between desktop fullscreen and windowed mode. */
    virtual bool setFullscreenDesktop(bool fullscreen)   = 0;
    /** @brief Switches between exclusive fullscreen and windowed mode. */
    virtual bool setFullscreenExclusive(bool fullscreen) = 0;

    virtual bool isOpen() const = 0;

    /** @brief Sets the window title shown in the OS title bar. */
    virtual void               setWindowTitle(const std::string& title) = 0;
    virtual const std::string& getWindowTitle() const                   = 0;
    /** @brief Moves the window to a position in logical units on the given display. */
    virtual void               setPosition(int x, int y, int display)   = 0;
    /** @brief Reads the current window position and display index. */
    virtual void               getPosition(int& x, int& y, int& display) = 0;
    /** @brief Minimizes / maximizes / restores the window. */
    virtual void               minimize()                               = 0;
    virtual void               maximize()                               = 0;
    virtual void               restore()                                = 0;
    virtual bool               isMaximized() const                      = 0;
    virtual bool               isMinimized() const                      = 0;
    /** @brief True when the window has keyboard focus. */
    virtual bool               hasFocus() const                         = 0;
    /** @brief True when the window has mouse focus. */
    virtual bool               hasMouseFocus() const                    = 0;
    virtual bool               isVisible() const                        = 0;
    /** @brief Enables/disables vertical sync (0 = off, 1 = on). */
    virtual void               setVSync(int vsync)                      = 0;
    virtual int                getVSync() const                         = 0;

    /** @brief Framebuffer (pixel) dimensions of the window. */
    virtual int    getPixelWidth() const  = 0;
    virtual int    getPixelHeight() const = 0;
    /** @brief Current UI/logical DPI scale (1.0 on non-retina displays). */
    virtual double getDPIScale() const    = 0;
    /** @brief Native DPI scale reported by the OS for this window. */
    virtual double getNativeDPIScale() const = 0;
    /** @brief Converts window logical coordinates to framebuffer pixel coordinates. */
    virtual void   windowToPixelCoords(double* x, double* y) const = 0;
    /** @brief Converts framebuffer pixel coordinates to window logical coordinates. */
    virtual void   pixelToWindowCoords(double* x, double* y) const = 0;
    /** @brief Converts window logical coordinates to UI-DPI coordinates. */
    virtual void   windowToDPICoords(double* x, double* y) const   = 0;
    /** @brief Converts UI-DPI coordinates to window logical coordinates. */
    virtual void   DPIToWindowCoords(double* x, double* y) const     = 0;
    /** @brief Scales a logical length to UI pixels (and back). */
    virtual double toPixels(double x) const                          = 0;
    virtual double fromPixels(double x) const                        = 0;
    /** @brief Vector variants of the coordinate conversions above. */
    virtual void   toPixelsXY(double wx, double wy, double& px, double& py) const   = 0;
    virtual void   fromPixelsXY(double px, double py, double& wx, double& wy) const = 0;

    /** @brief Native window handle (SDL_Window* on SDL backend). */
    virtual void *getHandle() const = 0;

    /** @brief Number of displays attached to the system. */
    virtual int                     getDisplayCount() const                      = 0;
    /** @brief Human-readable display name for a display index. */
    virtual std::string             getDisplayName(int display) const            = 0;
    /** @brief Display orientation string (e.g. "landscape"). */
    virtual std::string             getDisplayOrientation(int display) const     = 0;
    /** @brief Fullscreen sizes supported by a display. */
    virtual std::vector<WindowSize> getFullscreenSizes(int display) const        = 0;
    /** @brief Desktop resolution of a display. */
    virtual void getDesktopDimensions(int display, int& width, int& height) const = 0;

    /** @brief Shows a modal message box ("info", "warning", or "error"). */
    virtual bool showMessageBox(const std::string& title, const std::string& message,
                                const std::string& type, bool attachToWindow) = 0;
    /** @brief Shows a configurable message box; returns the pressed button index. */
    virtual int  showMessageBoxData(const MessageBoxData& data) = 0;
    /** @brief Requests OS attention (flashing taskbar); continuous repeats until focused. */
    virtual void requestAttention(bool continuous) = 0;

    /** @brief Sets the window icon from tightly packed RGBA8 pixels (w*h*4 bytes). */
    virtual bool setIconRGBA(const uint8_t* rgba, int width, int height) = 0;

    // --- not yet implemented (kept as drafts) ---

    // virtual bool onSizeChanged(int width, int height) = 0;

    // virtual Rect getSafeArea() const = 0;

    // virtual void setDisplaySleepEnabled(bool enable) = 0;
    // virtual bool isDisplaySleepEnabled() const = 0;

    // // default no-op implementation
    // virtual void swapBuffers();

    // virtual void setMouseGrab(bool grab) = 0;
    // virtual bool isMouseGrabbed() const = 0;

};  // Window

}  // namespace window
}  // namespace eve
