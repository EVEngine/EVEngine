#pragma once

#include <string>
#include <vector>

#include "common/Module.h"

namespace eve {

namespace graphics {
class Graphics;
}

namespace image {
class ImageData;
}

namespace window {

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
    bool     high_dpi      = false;
    bool     use_dpi_scale = true;
    bool     use_position  = false;
    bool     fullscreen    = false;
    bool     desktop_mode  = true;
    bool     stencil       = true;
    bool     resizable     = false;
    bool     always_on_top = false;
};

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

    virtual void setGraphics(graphics::Graphics* graphics) = 0;

    virtual void setSize(int width, int height) = 0;
    virtual int  getWidth() const               = 0;
    virtual int  getHeight() const              = 0;

    virtual bool           setWindowSettings(WindowSettings settings) = 0;
    virtual WindowSettings getWindowSettings()                        = 0;

    virtual void close() = 0;

    virtual bool setFullscreenDesktop(bool fullscreen)   = 0;
    virtual bool setFullscreenExclusive(bool fullscreen) = 0;

    virtual bool isOpen() const = 0;

    virtual void               setWindowTitle(const std::string& title) = 0;
    virtual const std::string& getWindowTitle() const                   = 0;
    virtual void               setPosition(int x, int y, int display)   = 0;
    virtual void               getPosition(int& x, int& y, int& display) = 0;
    virtual void               minimize()                               = 0;
    virtual void               maximize()                               = 0;
    virtual void               restore()                                = 0;
    virtual bool               isMaximized() const                      = 0;
    virtual bool               isMinimized() const                      = 0;
    virtual bool               hasFocus() const                         = 0;
    virtual bool               hasMouseFocus() const                    = 0;
    virtual bool               isVisible() const                        = 0;
    virtual void               setVSync(int vsync)                      = 0;
    virtual int                getVSync() const                         = 0;

    virtual int    getPixelWidth() const  = 0;
    virtual int    getPixelHeight() const = 0;
    virtual double getDPIScale() const    = 0;
    virtual double getNativeDPIScale() const = 0;
    virtual void   windowToPixelCoords(double* x, double* y) const = 0;
    virtual void   pixelToWindowCoords(double* x, double* y) const = 0;
    virtual void   windowToDPICoords(double* x, double* y) const   = 0;
    virtual void   DPIToWindowCoords(double* x, double* y) const     = 0;
    virtual double toPixels(double x) const                          = 0;
    virtual double fromPixels(double x) const                        = 0;
    virtual void   toPixelsXY(double wx, double wy, double& px, double& py) const   = 0;
    virtual void   fromPixelsXY(double px, double py, double& wx, double& wy) const = 0;

    /** Native window handle (SDL_Window* on SDL backend). */
    virtual void *getHandle() const = 0;

    virtual int                     getDisplayCount() const                      = 0;
    virtual std::string             getDisplayName(int display) const            = 0;
    virtual std::string             getDisplayOrientation(int display) const     = 0;
    virtual std::vector<WindowSize> getFullscreenSizes(int display) const        = 0;
    virtual void getDesktopDimensions(int display, int& width, int& height) const = 0;

    virtual bool showMessageBox(const std::string& title, const std::string& message,
                                const std::string& type, bool attachToWindow) = 0;
    virtual int  showMessageBoxData(const MessageBoxData& data) = 0;
    virtual void requestAttention(bool continuous) = 0;

    virtual bool              setIcon(image::ImageData *image_data) = 0;
    virtual image::ImageData *getIcon() const                       = 0;

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
