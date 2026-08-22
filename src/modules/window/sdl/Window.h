#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "window/Window.h"

namespace eve {
namespace window {
namespace sdl {

class Window final : public eve::window::Window {
public:
    Window();
    ~Window();

    void setSize(int w, int h) override;
    int  getWidth() const override;
    int  getHeight() const override;

    bool           setWindowSettings(WindowSettings settings) override;
    WindowSettings getWindowSettings() override;

    void close() override;

    bool setFullscreenDesktop(bool fullscreen) override;
    bool setFullscreenExclusive(bool fullscreen) override;

    bool isOpen() const override;

    void               setWindowTitle(const std::string& title) override;
    const std::string& getWindowTitle() const override;
    void               setPosition(int x, int y, int display) override;
    void               getPosition(int& x, int& y, int& display) override;
    void               minimize() override;
    void               maximize() override;
    void               restore() override;
    bool               isMaximized() const override;
    bool               isMinimized() const override;
    bool               hasFocus() const override;
    bool               hasMouseFocus() const override;
    bool               isVisible() const override;
    void               setVSync(int vsync) override;
    int                getVSync() const override;

    int    getPixelWidth() const override;
    int    getPixelHeight() const override;
    double getDPIScale() const override;
    double getNativeDPIScale() const override;
    void   windowToPixelCoords(double* x, double* y) const override;
    void   pixelToWindowCoords(double* x, double* y) const override;
    void   windowToDPICoords(double* x, double* y) const override;
    void   DPIToWindowCoords(double* x, double* y) const override;
    double toPixels(double x) const override;
    double fromPixels(double x) const override;
    void   toPixelsXY(double wx, double wy, double& px, double& py) const override;
    void   fromPixelsXY(double px, double py, double& wx, double& wy) const override;

    void *getHandle() const override { return window; }

    int                     getDisplayCount() const override;
    std::string             getDisplayName(int display) const override;
    std::string             getDisplayOrientation(int display) const override;
    std::vector<WindowSize> getFullscreenSizes(int display) const override;
    void getDesktopDimensions(int display, int& outWidth, int& outHeight) const override;

    bool showMessageBox(const std::string& caption, const std::string& message,
                        const std::string& type, bool attachToWindow) override;
    int  showMessageBoxData(const MessageBoxData& data) override;
    void requestAttention(bool continuous) override;

    bool setIconRGBA(const uint8_t* rgba, int w, int h) override;

    /** @brief Used by event backend on resize to refresh drawable size / viewport. */
    void updateSettings(const WindowSettings &newsettings, bool updateGraphicsViewport);

private:
    bool setFullscreenInternal(bool fullscreen, bool desktop_mode);

    int width = 800, height = 600;

    WindowSettings settings;

    std::string title;

    std::vector<uint8_t> iconRgba;
    int                  iconWidth  = 0;
    int                  iconHeight = 0;

    int windowWidth  = 800;
    int windowHeight = 600;
    int pixelWidth   = 800;
    int pixelHeight  = 600;

    bool open;
    bool mouseGrabbed;
    bool displayedWindowError;

    SDL_Window *window = nullptr;

    bool createWindowAndContext(int x, int y, int w, int h, Uint32 windowflags, int msaa, bool stencil, int depth);
    void close(bool allowExceptions);

};  // Window

}  // namespace sdl
}  // namespace window
}  // namespace eve
