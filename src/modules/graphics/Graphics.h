#pragma once

#include "common/Module.h"

namespace eve {
namespace graphics {

class Graphics : public Module {
public:
    Module_REG;
    virtual ~Graphics() {}

    /**
	 * Resets the current color, background color, line style, and so forth.
	 **/
	void reset();

	virtual void clear(OptionalColorf color, OptionalInt stencil, OptionalDouble depth) = 0;
	virtual void clear(const std::vector<OptionalColorf> &colors, OptionalInt stencil, OptionalDouble depth) = 0;

    virtual void present() = 0;

    /**
	 * Sets the current graphics display viewport dimensions.
	 **/
	virtual void setViewportSize(int width, int height, int pixelwidth, int pixelheight) = 0;

    int getWidth() const;
	int getHeight() const;
	int getPixelWidth() const;
	int getPixelHeight() const;

	double getCurrentDPIScale() const;
	double getScreenDPIScale() const;


    /**
	 * Sets the current constant color.
	 **/
	virtual void setColor(Colorf c) = 0;

	/**
	 * Gets current color.
	 **/
	Colorf getColor() const;

    	/**
	 * Sets the background Color.
	 **/
	void setBackgroundColor(Colorf c);

	/**
	 * Gets the current background color.
	 **/
	Colorf getBackgroundColor() const;

	void setFont(Font *font);
	Font *getFont();

	void setShader(Shader *shader);
	void setShader();

	Shader *getShader() const;

	void setCanvas(RenderTarget rt, uint32 temporaryRTFlags);
	void setCanvas();

    virtual void setDepthMode(CompareMode compare, bool write) = 0;
	void setDepthMode();
	void getDepthMode(CompareMode &compare, bool &write) const;

	/**
	 * Sets the current blend mode.
	 **/
	virtual void setBlendMode(BlendMode mode, BlendAlpha alphamode) = 0;

	/**
	 * Gets the current blend mode.
	 **/
	BlendMode getBlendMode(BlendAlpha &alphamode) const;


    /**
	 * Sets the default filter for images, canvases, and fonts.
	 **/
	void setDefaultFilter(const Texture::Filter &f);

	/**
	 * Gets the default filter for images, canvases, and fonts.
	 **/
	const Texture::Filter &getDefaultFilter() const;

    /**
	 * Default Image mipmap filter mode and sharpness values.
	 **/
	void setDefaultMipmapFilter(Texture::FilterMode filter, float sharpness);
	void getDefaultMipmapFilter(Texture::FilterMode *filter, float *sharpness) const;


    /**
	 * Sets the line width.
	 * @param width The new width of the line.
	 **/
	void setLineWidth(float width);
	float getLineWidth() const;

	/**
	 * Sets the line style.
	 * @param style LINE_ROUGH or LINE_SMOOTH.
	 **/
	void setLineStyle(LineStyle style);
	LineStyle getLineStyle() const;

	/**
	 * Sets the line join mode.
	 **/
	void setLineJoin(LineJoin style);
	LineJoin getLineJoin() const;

	/**
	 * Sets the size of points.
	 **/
	virtual void setPointSize(float size) = 0;

	/**
	 * Gets the point size.
	 **/
	float getPointSize() const;

	/**
	 * Sets whether graphics will be drawn as wireframe lines instead of filled
	 * triangles (has no effect for drawn points.)
	 * This should only be used as a debugging tool. The wireframe lines do not
	 * behave the same as regular love.graphics lines.
	 **/
	virtual void setWireframe(bool enable) = 0;

    /**
	 * Gets whether wireframe drawing mode is enabled.
	 **/
	bool isWireframe() const;


    void draw(Drawable *drawable, const Matrix4 &m);
	void draw(Texture *texture, Quad *quad, const Matrix4 &m);
	void drawLayer(Texture *texture, int layer, const Matrix4 &m);
	void drawLayer(Texture *texture, int layer, Quad *quad, const Matrix4 &m);
	void drawInstanced(Mesh *mesh, const Matrix4 &m, int instancecount);

	/**
	 * Draws text at the specified coordinates
	 **/
	void print(const std::vector<Font::ColoredString> &str, const Matrix4 &m);
	void print(const std::vector<Font::ColoredString> &str, Font *font, const Matrix4 &m);

	/**
	 * Draws formatted text on screen at the specified coordinates.
	 **/
	void printf(const std::vector<Font::ColoredString> &str, float wrap, Font::AlignMode align, const Matrix4 &m);
	void printf(const std::vector<Font::ColoredString> &str, Font *font, float wrap, Font::AlignMode align, const Matrix4 &m);

	/**
	 * Draws a series of points at the specified positions.
	 **/
	void points(const Vector2 *positions, const Colorf *colors, size_t numpoints);

	/**
	 * Draws a series of lines connecting the given vertices.
	 * @param coords Vertex positions (v1, ..., vn). If v1 == vn the line will be drawn closed.
	 * @param count Number of vertices.
	 **/
	void polyline(const Vector2 *vertices, size_t count);

	/**
	 * Draws a rectangle.
	 * @param x Position along x-axis for top-left corner.
	 * @param y Position along y-axis for top-left corner.
	 * @param w The width of the rectangle.
	 * @param h The height of the rectangle.
	 **/
	void rectangle(DrawMode mode, float x, float y, float w, float h);

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
	void rectangle(DrawMode mode, float x, float y, float w, float h, float rx, float ry, int points);
	void rectangle(DrawMode mode, float x, float y, float w, float h, float rx, float ry);

	/**
	 * Draws a circle using the specified arguments.
	 * @param mode The mode of drawing (line/filled).
	 * @param x X-coordinate.
	 * @param y Y-coordinate.
	 * @param radius Radius of the circle.
	 * @param points Number of points to use to draw the circle.
	 **/
	void circle(DrawMode mode, float x, float y, float radius, int points);
	void circle(DrawMode mode, float x, float y, float radius);

	/**
	 * Draws an ellipse using the specified arguments.
	 * @param mode The mode of drawing (line/filled).
	 * @param x X-coordinate of center
	 * @param y Y-coordinate of center
	 * @param a Radius in x-direction
	 * @param b Radius in y-direction
	 * @param points Number of points to use to draw the circle.
	 **/
	void ellipse(DrawMode mode, float x, float y, float a, float b, int points);
	void ellipse(DrawMode mode, float x, float y, float a, float b);

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
	void arc(DrawMode drawmode, ArcMode arcmode, float x, float y, float radius, float angle1, float angle2, int points);
	void arc(DrawMode drawmode, ArcMode arcmode, float x, float y, float radius, float angle1, float angle2);

	/**
	 * Draws a polygon with an arbitrary number of vertices.
	 * @param mode The type of drawing (line/filled).
	 * @param coords Vertex positions.
	 * @param count Vertex array size.
	 **/
	void polygon(DrawMode mode, const Vector2 *vertices, size_t count, bool skipLastFilledVertex = true);


    void push(StackType type = STACK_TRANSFORM);
	void pop();

	const Matrix4 &getTransform() const;
	const Matrix4 &getProjection() const;

	void rotate(float r);
	void scale(float x, float y = 1.0f);
	void translate(float x, float y);
	void shear(float kx, float ky);
	void origin();

	void applyTransform(love::math::Transform *transform);
	void replaceTransform(love::math::Transform *transform);

	Vector2 transformPoint(Vector2 point);
	Vector2 inverseTransformPoint(Vector2 point);

	virtual void draw(const DrawCommand &cmd) = 0;
	virtual void draw(const DrawIndexedCommand &cmd) = 0;
	virtual void drawQuads(int start, int count, const vertex::Attributes &attributes, const vertex::BufferBindings &buffers, Texture *texture) = 0;

protected:

	struct DisplayState
	{
		Colorf color = Colorf(1.0, 1.0, 1.0, 1.0);
		Colorf backgroundColor = Colorf(0.0, 0.0, 0.0, 1.0);

		BlendMode blendMode = BLEND_ALPHA;
		BlendAlpha blendAlphaMode = BLENDALPHA_MULTIPLY;

		float lineWidth = 1.0f;
		LineStyle lineStyle = LINE_SMOOTH;
		LineJoin lineJoin = LINE_JOIN_MITER;

		float pointSize = 1.0f;

		bool scissor = false;
		Rect scissorRect = Rect();

		CompareMode stencilCompare = COMPARE_ALWAYS;
		int stencilTestValue = 0;

		CompareMode depthTest = COMPARE_ALWAYS;
		bool depthWrite = false;

		CullMode meshCullMode = CULL_NONE;
		vertex::Winding winding = vertex::WINDING_CCW;

		StrongRef<Font> font;
		StrongRef<Shader> shader;

		RenderTargetsStrongRef renderTargets;

		ColorMask colorMask = ColorMask(true, true, true, true);

		bool wireframe = false;

		Texture::Filter defaultFilter = Texture::Filter();

		Texture::FilterMode defaultMipmapFilter = Texture::FILTER_LINEAR;
		float defaultMipmapSharpness = 0.0f;
	};

    std::vector<DisplayState> states;
};

}  // namespace graphics
}  // namespace eve
