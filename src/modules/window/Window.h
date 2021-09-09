#pragma once

#include "common/Module.h"
#include "common/Math.h"

#include <string>
#include <vector>




namespace eve
{

namespace graphics {
    class Graphics;
}

namespace window
{


struct WindowSettings;

class Window : public Module
{
public:
	SSQ_REG

	// Different window settings.
	enum class Setting
	{
		FULLSCREEN,
		FULLSCREEN_TYPE,
		VSYNC,
		MSAA,
		STENCIL,
		DEPTH,
		RESIZABLE,
		MIN_WIDTH,
		MIN_HEIGHT,
		BORDERLESS,
		CENTERED,
		DISPLAY,
		HIGHDPI,
		USE_DPISCALE,
		REFRESHRATE,
		X,
		Y,
		MAX_ENUM
	};

	enum class FullscreenType
	{
		EXCLUSIVE,
		DESKTOP,
		MAX_ENUM
	};

	enum class MessageBoxType
	{
		ERROR,
		WARNING,
		INFO,
		MAX_ENUM
	};

	enum class DisplayOrientation
	{
		UNKNOWN,
		LANDSCAPE,
		LANDSCAPE_FLIPPED,
		PORTRAIT,
		PORTRAIT_FLIPPED,
		MAX_ENUM
	};

	struct WindowSize
	{
		int width;
		int height;

		bool operator == (const WindowSize &w) const
		{
			return w.width == width && w.height == height;
		}
	};

	struct MessageBoxData
	{
		MessageBoxType type;

		std::string title;
		std::string message;

		std::vector<std::string> buttons;
		int enterButtonIndex;
		int escapeButtonIndex;

		bool attachToWindow;
	};

	virtual ~Window() {}

	// Implements Module.
	virtual uint32_t getModuleType() const { return (uint32_t) ModuleType::window; }

	static Window* create();


	// virtual void setGraphics(graphics::Graphics *graphics) = 0;

	// virtual bool setWindowSettings(int width = 800, int height = 600, const WindowSettings *settings = nullptr) = 0;
	// virtual void getWindowSettings(int &width, int &height, WindowSettings &settings) = 0;

	// virtual void close() = 0;

	// virtual bool setFullscreen(bool fullscreen, FullscreenType fstype) = 0;
	// virtual bool setFullscreen(bool fullscreen) = 0;

	// virtual bool onSizeChanged(int width, int height) = 0;

	// virtual int getDisplayCount() const = 0;

	// virtual const char *getDisplayName(int display) const = 0;

	// virtual DisplayOrientation getDisplayOrientation(int display) const = 0;

	// virtual std::vector<WindowSize> getFullscreenSizes(int display) const = 0;

	// virtual void getDesktopDimensions(int display, int &width, int &height) const = 0;

	// virtual void setPosition(int x, int y, int display) = 0;
	// virtual void getPosition(int &x, int &y, int &display) = 0;

	// virtual Rect getSafeArea() const = 0;

	// virtual bool isOpen() const = 0;

	// virtual void setWindowTitle(const std::string &title) = 0;
	// virtual const std::string &getWindowTitle() const = 0;

	// // virtual bool setIcon(image::ImageData *image_data) = 0;
	// // virtual image::ImageData *getIcon() = 0;

	// virtual void setVSync(int vsync) = 0;
	// virtual int getVSync() const = 0;

	// virtual void setDisplaySleepEnabled(bool enable) = 0;
	// virtual bool isDisplaySleepEnabled() const = 0;

	// virtual void minimize() = 0;
	// virtual void maximize() = 0;
	// virtual void restore() = 0;

	// virtual bool isMaximized() const = 0;
	// virtual bool isMinimized() const = 0;

	// // default no-op implementation
	// virtual void swapBuffers();

	// virtual bool hasFocus() const = 0;
	// virtual bool hasMouseFocus() const = 0;

	// virtual bool isVisible() const = 0;

	// virtual void setMouseGrab(bool grab) = 0;
	// virtual bool isMouseGrabbed() const = 0;

	// virtual int getWidth() const = 0;
	// virtual int getHeight() const = 0;
	// virtual int getPixelWidth() const = 0;
	// virtual int getPixelHeight() const = 0;

	// // Note: window-space coordinates are not necessarily the same as
	// // density-independent units (which toPixels and fromPixels use.)
	// virtual void windowToPixelCoords(double *x, double *y) const = 0;
	// virtual void pixelToWindowCoords(double *x, double *y) const = 0;

	// virtual void windowToDPICoords(double *x, double *y) const = 0;
	// virtual void DPIToWindowCoords(double *x, double *y) const = 0;

	// virtual double getDPIScale() const = 0;
	// virtual double getNativeDPIScale() const = 0;

	// virtual double toPixels(double x) const = 0;
	// virtual void toPixels(double wx, double wy, double &px, double &py) const = 0;
	// virtual double fromPixels(double x) const = 0;
	// virtual void fromPixels(double px, double py, double &wx, double &wy) const = 0;

	// virtual const void *getHandle() const = 0;

	// virtual bool showMessageBox(const std::string &title, const std::string &message, MessageBoxType type, bool attachtowindow) = 0;
	// virtual int showMessageBox(const MessageBoxData &data) = 0;

	// virtual void requestAttention(bool continuous) = 0;

}; // Window

struct WindowSettings
{
	bool fullscreen = false;
	Window::FullscreenType fstype = Window::FullscreenType::DESKTOP;
	int vsync = 1;
	int msaa = 0;
	bool stencil = true;
	int depth = 0;
	bool resizable = false;
	int minwidth = 1;
	int minheight = 1;
	bool borderless = false;
	bool centered = true;
	int display = 0;
	bool highdpi = false;
	bool usedpiscale = true;
	double refreshrate = 0.0;
	bool useposition = false;
	int x = 0;
	int y = 0;
};



} // namespace window


} // namespace eve
