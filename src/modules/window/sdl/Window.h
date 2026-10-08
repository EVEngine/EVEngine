#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "window/Window.h"

namespace eve {
namespace window {
namespace sdl {

/** @brief SDL2 desktop/mobile implementation of eve::window::Window. */
class Window final : public eve::window::Window {
public:
    /** @brief Constructs an unopened window module; call setWindowSettings to create. */
    Window();
    /** @brief Destroys the SDL_Window if still open. */
    ~Window();

    /** @brief @copydoc eve::window::Window::setSize */
    void setSize(int w, int h) override;
    /** @brief @copydoc eve::window::Window::getWidth */
    int  getWidth() const override;
    /** @brief @copydoc eve::window::Window::getHeight */
    int  getHeight() const override;

    /** @brief @copydoc eve::window::Window::setWindowSettings */
    bool           setWindowSettings(WindowSettings settings) override;
    /** @brief @copydoc eve::window::Window::getWindowSettings */
    WindowSettings getWindowSettings() override;

    /** @brief @copydoc eve::window::Window::close */
    void close() override;

    /** @brief @copydoc eve::window::Window::setFullscreenDesktop */
    bool setFullscreenDesktop(bool fullscreen) override;
    /** @brief @copydoc eve::window::Window::setFullscreenExclusive */
    bool setFullscreenExclusive(bool fullscreen) override;

    /** @brief @copydoc eve::window::Window::isOpen */
    bool isOpen() const override;

    /** @brief @copydoc eve::window::Window::setWindowTitle */
    void               setWindowTitle(const std::string& title) override;
    /** @brief @copydoc eve::window::Window::getWindowTitle */
    const std::string& getWindowTitle() const override;
    /** @brief @copydoc eve::window::Window::setPosition */
    void               setPosition(int x, int y, int display) override;
    /** @brief @copydoc eve::window::Window::getPosition */
    void               getPosition(int& x, int& y, int& display) override;
    /** @brief @copydoc eve::window::Window::minimize */
    void               minimize() override;
    /** @brief @copydoc eve::window::Window::maximize */
    void               maximize() override;
    /** @brief @copydoc eve::window::Window::restore */
    void               restore() override;
    /** @brief @copydoc eve::window::Window::isMaximized */
    bool               isMaximized() const override;
    /** @brief @copydoc eve::window::Window::isMinimized */
    bool               isMinimized() const override;
    /** @brief @copydoc eve::window::Window::hasFocus */
    bool               hasFocus() const override;
    /** @brief @copydoc eve::window::Window::hasMouseFocus */
    bool               hasMouseFocus() const override;
    /** @brief @copydoc eve::window::Window::isVisible */
    bool               isVisible() const override;
    /** @brief @copydoc eve::window::Window::setVSync */
    void               setVSync(int vsync) override;
    /** @brief @copydoc eve::window::Window::getVSync */
    int                getVSync() const override;

    /** @brief @copydoc eve::window::Window::getPixelWidth */
    int    getPixelWidth() const override;
    /** @brief @copydoc eve::window::Window::getPixelHeight */
    int    getPixelHeight() const override;
    /** @brief @copydoc eve::window::Window::getDPIScale */
    double getDPIScale() const override;
    /** @brief @copydoc eve::window::Window::getNativeDPIScale */
    double getNativeDPIScale() const override;
    /** @brief @copydoc eve::window::Window::windowToPixelCoords */
    void   windowToPixelCoords(double* x, double* y) const override;
    /** @brief @copydoc eve::window::Window::pixelToWindowCoords */
    void   pixelToWindowCoords(double* x, double* y) const override;
    /** @brief @copydoc eve::window::Window::windowToDPICoords */
    void   windowToDPICoords(double* x, double* y) const override;
    /** @brief @copydoc eve::window::Window::DPIToWindowCoords */
    void   DPIToWindowCoords(double* x, double* y) const override;
    /** @brief @copydoc eve::window::Window::toPixels */
    double toPixels(double x) const override;
    /** @brief @copydoc eve::window::Window::fromPixels */
    double fromPixels(double x) const override;
    /** @brief @copydoc eve::window::Window::toPixelsXY */
    void   toPixelsXY(double wx, double wy, double& px, double& py) const override;
    /** @brief @copydoc eve::window::Window::fromPixelsXY */
    void   fromPixelsXY(double px, double py, double& wx, double& wy) const override;

    /** @brief @copydoc eve::window::Window::getHandle */
    void *getHandle() const override { return window; }

    /** @brief @copydoc eve::window::Window::getDisplayCount */
    int                     getDisplayCount() const override;
    /** @brief @copydoc eve::window::Window::getDisplayName */
    std::string             getDisplayName(int display) const override;
    /** @brief @copydoc eve::window::Window::getDisplayOrientation */
    std::string             getDisplayOrientation(int display) const override;
    /** @brief @copydoc eve::window::Window::getFullscreenSizes */
    std::vector<WindowSize> getFullscreenSizes(int display) const override;
    /** @brief @copydoc eve::window::Window::getDesktopDimensions */
    void getDesktopDimensions(int display, int& outWidth, int& outHeight) const override;

    /** @brief @copydoc eve::window::Window::showMessageBox */
    bool showMessageBox(const std::string& caption, const std::string& message,
                        const std::string& type, bool attachToWindow) override;
    /** @brief @copydoc eve::window::Window::showMessageBoxData */
    int  showMessageBoxData(const MessageBoxData& data) override;
    /** @brief @copydoc eve::window::Window::requestAttention */
    void requestAttention(bool continuous) override;

    /** @brief @copydoc eve::window::Window::setIconRGBA */
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
