#pragma once

#include "common/Module.h"
#include "graphics/Shader.h"
#include "graphics/Drawable.h"
#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

class Graphics : public Module, public Canvas {
public:
    Module_REG(Graphics);
    virtual ~Graphics() {}

    /**
	 * Resets the current color, background color, line style, and so forth.
	 **/
	void reset();

	
    virtual void present() = 0;

    /**
     * Bind to an existing native window (SDL_Window*) and create Vulkan device/swapchain.
     * Must be called after the window exists (SDL_WINDOW_VULKAN).
     **/
    virtual void initWithWindow(void *nativeWindow) = 0;

    /**
     * Sets the current graphics display viewport dimensions.
     **/
    virtual void setViewportSize(int width, int height, int pixelwidth, int pixelheight) = 0;

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getPixelWidth() const { return pixelWidth; }
    int getPixelHeight() const { return pixelHeight; }

    double getCurrentDPIScale() const {
        return (width > 0) ? double(pixelWidth) / double(width) : 1.0;
    }
    double getScreenDPIScale() const { return getCurrentDPIScale(); }

    /** Internal immediate-mode helper used by RenderSystem / Batcher. */
    virtual void drawSolidRect(float x, float y, float w, float h, const Color &color) = 0;

    /** Create RGBA8 texture from CPU pixels (size = width*height*4). Caller owns Texture*. */
    virtual Texture *newTexture(int width, int height, const uint8_t *rgba) = 0;

    /** Create texture from ImageData (RGBA8 required for now). */
    virtual Texture *newTexture(image::ImageData *data) = 0;

    /** Load file via Filesystem + Image decode, then upload (RGBA8). Throws on failure. */
    virtual Texture *newTextureFromFile(const std::string &filename) = 0;

    /** Draw a textured quad (full UV 0..1). texture may be null → solid. */
    virtual void drawTexturedRect(Texture *texture, float x, float y, float w, float h,
                                  const Color &color) = 0;

    Color getBackgroundColor() const { return backgroundColor; }
    void setBackgroundColor(const Color &c) { backgroundColor = c; }


	// void setFont(Font *font);
	// Font *getFont();

	void setShader(Shader *shader);
	void setShader();

	Shader *getShader() const;

    /** Create an offscreen render target (sampleable). Owned by Graphics. */
    virtual Canvas *newCanvas(int width, int height) = 0;

    /** nullptr or this → screen. Switching flushes pending draws to the previous target. */
    virtual void setCanvas(Canvas *canvas) = 0;
    void setCanvas() { setCanvas(nullptr); }

    virtual bool isCanvasActive() const = 0;
    virtual Canvas *getCanvas() const = 0;

    void draw(Drawable *drawable, const glm::mat4 &m);
	// void draw(Texture *texture, Quad *quad, const glm::mat4 &m);
	// void drawLayer(Texture *texture, int layer, const glm::mat4 &m);
	// void drawLayer(Texture *texture, int layer, Quad *quad, const glm::mat4 &m);
	// void drawInstanced(Mesh *mesh, const glm::mat4 &m, int instancecount);


	/**
	 * Draws a series of points at the specified positions.
	 **/
	void points(const std::vector<glm::vec2>& positions, const std::vector<Color>& colors);

	/**
	 * Draws a series of lines connecting the given vertices.
	 * @param coords Vertex positions (v1, ..., vn). If v1 == vn the line will be drawn closed.
	 * @param count Number of vertices.
	 **/
	void polyline(const glm::mat4 *vertices, size_t count);

	/**
	 * Draws a rectangle.
	 * @param x Position along x-axis for top-left corner.
	 * @param y Position along y-axis for top-left corner.
	 * @param w The width of the rectangle.
	 * @param h The height of the rectangle.
	 **/
	void rectangle(std::string mode, float x, float y, float w, float h);

	/**
	 * Variant of rectangle that draws a rounded rectangle.
	 * @param mode The mode of drawing (line/filled).
	 * @param x X-coordinate of top-left corner
	 * @param y Y-coordinate of top-left corner
	 * @param w The width of the rectangle.
	 * @param h The height of the rectangle.
	 * @param rx The radius of the corners on the x axis
	 * @param ry The radius of the corners on the y axis
	 * @param points The number of points to use per corner
	 **/
	void rectangle(std::string mode, float x, float y, float w, float h, float rx, float ry, int points);
	void rectangle(std::string mode, float x, float y, float w, float h, float rx, float ry);

	/**
	 * Draws a circle using the specified arguments.
	 * @param mode The mode of drawing (line/filled).
	 * @param x X-coordinate.
	 * @param y Y-coordinate.
	 * @param radius Radius of the circle.
	 * @param points Number of points to use to draw the circle.
	 **/
	void circle(std::string mode, float x, float y, float radius, int points);
	void circle(std::string mode, float x, float y, float radius);

	/**
	 * Draws an ellipse using the specified arguments.
	 * @param mode The mode of drawing (line/filled).
	 * @param x X-coordinate of center
	 * @param y Y-coordinate of center
	 * @param a Radius in x-direction
	 * @param b Radius in y-direction
	 * @param points Number of points to use to draw the circle.
	 **/
	void ellipse(std::string mode, float x, float y, float a, float b, int points);
	void ellipse(std::string mode, float x, float y, float a, float b);

	/**
	 * Draws an arc using the specified arguments.
	 * @param drawmode The mode of drawing (line/filled).
	 * @param arcmode The type of arc.
	 * @param x X-coordinate.
	 * @param y Y-coordinate.
	 * @param radius Radius of the arc.
	 * @param angle1 The angle at which the arc begins.
	 * @param angle2 The angle at which the arc terminates.
	 * @param points Number of points to use to draw the arc.
	 **/
	void arc(std::string mode, std::string arcmode, float x, float y, float radius, float angle1, float angle2, int points);
	void arc(std::string mode, std::string arcmode, float x, float y, float radius, float angle1, float angle2);

	/**
	 * Draws a polygon with an arbitrary number of vertices.
	 * @param mode The type of drawing (line/filled).
	 * @param coords Vertex positions.
	 * @param count Vertex array size.
	 **/
	void polygon(std::string mode, const std::vector<glm::vec2>& vertices, bool skipLastFilledVertex = true);


    void push(bool all);
	void pop();

	const glm::mat4 &getTransform() const;
	const glm::mat4 &getProjection() const;

	void rotate(float r);
	void scale(float x, float y = 1.0f);
	void translate(float x, float y);
	void origin();

	// void applyTransform(love::math::Transform *transform);
	// void replaceTransform(love::math::Transform *transform);

	glm::vec2 transformPoint(glm::vec2 point);
	glm::vec2 inverseTransformPoint(glm::vec2 point);

	// virtual void draw(const DrawCommand &cmd) = 0;
	// virtual void draw(const DrawIndexedCommand &cmd) = 0;
	// virtual void drawQuads(int start, int count, const vertex::Attributes &attributes, const vertex::BufferBindings &buffers, Texture *texture) = 0;

protected:
    int width = 0;
    int height = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    Color backgroundColor{0.1f, 0.1f, 0.12f, 1.0f};
};

}  // namespace graphics
